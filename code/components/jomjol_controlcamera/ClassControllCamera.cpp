#include <stdio.h>
#include <string.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_system.h>
#include <nvs_flash.h>
#include <sys/param.h>
#include <sys/stat.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <driver/gpio.h>
#include <driver/ledc.h>

#include <esp_timer.h>
#include <esp_log.h>

#include "statusled.h"
#include "CImageBasis.h"

#include "server_ota.h"
#include "server_GpioPin.h"
#include "server_GpioHandler.h"

#include "ClassControllCamera.h"
#include "ClassLogFile.h"
#include "MainFlowControl.h"

#if (ESP_IDF_VERSION_MAJOR >= 5)
#include "soc/periph_defs.h"
#include "esp_private/periph_ctrl.h"
#include "soc/gpio_sig_map.h"
#include "soc/gpio_periph.h"
#include "soc/io_mux_reg.h"
#include "esp_rom_gpio.h"

#define gpio_pad_select_gpio esp_rom_gpio_pad_select_gpio
#define gpio_matrix_in(a, b, c) esp_rom_gpio_connect_in_signal(a, b, c)
#define gpio_matrix_out(a, b, c, d) esp_rom_gpio_connect_out_signal(a, b, c, d)
#define ets_delay_us(a) esp_rom_delay_us(a)
#endif

static const char *TAG = "CAM";

CCamera Camera;
camera_controll_config_t CameraConfigSaved;
camera_controll_config_t CameraConfigDefault;
camera_controll_config_t CameraConfigTemp;

/* Camera live stream */
#define PART_BOUNDARY "123456789000000000000987654321"
static const char *_STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char *_STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char *_STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

uint8_t *tempImage = NULL; // Buffer holding the demo image in bytes
std::vector<std::string> demoFiles;

// Camera module bus communications frequency.
// Originally: config.xclk_freq_mhz = 20000000, but this lead to visual artifacts on many modules.
// See https://github.com/espressif/esp32-camera/issues/150#issuecomment-726473652 et al.
#if !defined(XCLK_FREQ_MHZ)
int xclk = 10;
// int xclk = 20; // Orginal value
#else
int xclk = XCLK_FREQ_MHZ;
#endif

static camera_config_t camera_config = {
    .pin_pwdn = CAM_PIN_PWDN,
    .pin_reset = CAM_PIN_RESET,

    .pin_xclk = CAM_PIN_XCLK,
    .pin_sscb_sda = CAM_PIN_SIOD,
    .pin_sscb_scl = CAM_PIN_SIOC,

    .pin_d7 = CAM_PIN_D7,
    .pin_d6 = CAM_PIN_D6,
    .pin_d5 = CAM_PIN_D5,
    .pin_d4 = CAM_PIN_D4,
    .pin_d3 = CAM_PIN_D3,
    .pin_d2 = CAM_PIN_D2,
    .pin_d1 = CAM_PIN_D1,
    .pin_d0 = CAM_PIN_D0,

    .pin_vsync = CAM_PIN_VSYNC,
    .pin_href = CAM_PIN_HREF,
    .pin_pclk = CAM_PIN_PCLK,

    .xclk_freq_hz = (xclk * 1000000),
    .ledc_timer = CAM_XCLK_TIMER,     // LEDC timer to be used for generating XCLK
    .ledc_channel = CAM_XCLK_CHANNEL, // LEDC channel to be used for generating XCLK

    .pixel_format = PIXFORMAT_JPEG,      // YUV422,GRAYSCALE,RGB565,JPEG
    .frame_size = FRAMESIZE_VGA,         // QQVGA-UXGA Do not use sizes above QVGA when not JPEG
    .jpeg_quality = 12,                  // 0-63 lower number means higher quality
    .fb_count = 1,                       // if more than one, i2s runs in continuous mode. Use only with JPEG
    .fb_location = CAMERA_FB_IN_PSRAM,   // The location where the frame buffer will be allocated
    .grab_mode = CAMERA_GRAB_WHEN_EMPTY, // CAMERA_GRAB_LATEST. Sets when buffers should be filled

#if CONFIG_CAMERA_CONVERTER_ENABLED
    .conv_mode = CONV_DISABLE, // RGB<->YUV Conversion mode
#endif

    .sccb_i2c_port = 0, // If pin_sccb_sda is -1, use the already configured I2C bus by number
};

CCamera::CCamera(void)
{
#ifdef DEBUG_DETAIL_ON
    ESP_LOGD(TAG, "CreateClassCamera");
#endif
}

esp_err_t CCamera::InitCam(void)
{
    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "InitCam - Start");

    // De-init in case it was already initialized
    esp_err_t err = esp_camera_deinit();
    vTaskDelay(500 / portTICK_PERIOD_MS);
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "Camera Denit Failed");
    }

    // initialize the camera
    err = esp_camera_init(&camera_config);
    vTaskDelay(500 / portTICK_PERIOD_MS);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Camera Init Failed");
        return err;
    }

    Camera.CamInitSuccessful = false;

    // Get a reference to the sensor
    sensor_t *sensor = esp_camera_sensor_get();
    if (sensor != NULL)
    {
        // Camera.CamSensor_id = sensor->id.PID;
        Camera.GetSensorControllConfig(&CameraConfigDefault); // Kamera >>> CameraConfigDefault
        Camera.SetCameraConfigFromTo(&CameraConfigDefault, &CameraConfigSaved);
        Camera.SetCameraConfigFromTo(&CameraConfigDefault, &CameraConfigTemp);

        // Dump camera module, warn for unsupported modules.
        switch (Camera.CamSensor_id)
        {
        case OV2640_PID:
        {
            ESP_LOGI(TAG, "OV2640 camera module detected");
            Camera.CamInitSuccessful = true;
        }
        break;
#if CONFIG_OV3660_SUPPORT
        case OV3660_PID:
        {
            ESP_LOGI(TAG, "OV3660 camera module detected");
            Camera.CamInitSuccessful = true;
        }
        break;
#endif
#if CONFIG_OV5640_SUPPORT
        case OV5640_PID:
        {
            ESP_LOGI(TAG, "OV5640 camera module detected");
            Camera.CamInitSuccessful = true;
        }
        break;
#endif
        default:
        {
            ESP_LOGE(TAG, "Camera module is unknown and not properly supported!");
        }
        break;
        }
    }

    if (!Camera.CamInitSuccessful)
    {
        return ESP_FAIL;
    }

    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "InitCam - securely ended");

    return ESP_OK;
}

void CCamera::ResetCamera(void)
{
    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "ResetCamera - Start");

#if CAM_PIN_RESET == GPIO_NUM_NC
    sensor_t *sensor = esp_camera_sensor_get();
    if (sensor != NULL)
    {
        if (sensor->id.PID == OV2640_PID)
        {
            sensor->set_reg(sensor, 0x12, 0xff, 0x80);
            vTaskDelay(100 / portTICK_PERIOD_MS);
            sensor->set_reg(sensor, 0xff, 0xff, 0x01);
        }
        else if ((sensor->id.PID == OV3660_PID) || (sensor->id.PID == OV5640_PID))
        {
            sensor->set_reg(sensor, 0x3008, 0xff, 0x82);
        }
        else
        {
            sensor->reset(sensor);
        }
    }
    vTaskDelay(100 / portTICK_PERIOD_MS);
    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "No reset pin availbale to reset camera");
#else // Use reset only if pin is available
    ESP_LOGD(TAG, "Resetting camera");
    gpio_config_t conf;
    conf.intr_type = GPIO_INTR_DISABLE;
    conf.pin_bit_mask = 1LL << CAM_PIN_RESET;
    conf.mode = GPIO_MODE_OUTPUT;
    conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&conf);

    // carefull, logic is inverted compared to reset pin
    gpio_set_level(CAM_PIN_RESET, 1);
    vTaskDelay(10 / portTICK_PERIOD_MS);
    gpio_set_level(CAM_PIN_RESET, 0);
    vTaskDelay(10 / portTICK_PERIOD_MS);
#endif

    InitCam();

    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "ResetCamera - securely ended");
}

void CCamera::PowerResetCamera(void)
{
    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "PowerResetCamera - Start");

#if CAM_PIN_PWDN == GPIO_NUM_NC
    sensor_t *sensor = esp_camera_sensor_get();
    if (sensor != NULL)
    {
        if (sensor->id.PID == OV2640_PID)
        {
            sensor->set_reg(sensor, 0x12, 0xff, 0x80);
            vTaskDelay(100 / portTICK_PERIOD_MS);
            sensor->set_reg(sensor, 0xff, 0xff, 0x01);
        }
        else if ((sensor->id.PID == OV3660_PID) || (sensor->id.PID == OV5640_PID))
        {
            sensor->set_reg(sensor, 0x3008, 0xff, 0x82);
        }
        else
        {
            sensor->reset(sensor);
        }
    }
    vTaskDelay(100 / portTICK_PERIOD_MS);
    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "No power down pin availbale to reset camera");
#else // Use reset only if pin is available
    ESP_LOGD(TAG, "Resetting camera by power down line");
    gpio_config_t conf;
    conf.intr_type = GPIO_INTR_DISABLE;
    conf.pin_bit_mask = 1LL << CAM_PIN_PWDN;
    conf.mode = GPIO_MODE_OUTPUT;
    conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&conf);

    // carefull, logic is inverted compared to reset pin
    gpio_set_level(CAM_PIN_PWDN, 1);
    vTaskDelay(10 / portTICK_PERIOD_MS);
    gpio_set_level(CAM_PIN_PWDN, 0);
    vTaskDelay(10 / portTICK_PERIOD_MS);
#endif

    InitCam();

    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "PowerResetCamera - securely ended");
}

bool CCamera::testCamera(void)
{
    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "testCamera - Start");

    bool success;

    Camera.FlashLightOnOff(true, Camera.LedIntensity);
    vTaskDelay(500 / portTICK_PERIOD_MS);

    camera_fb_t *fb = esp_camera_fb_get();

    if (fb)
    {
        success = true;
    }
    else
    {
        success = false;
    }

    // esp_camera_fb_return(fb);
    esp_camera_return_all();
    Camera.FlashLightOnOff(false, Camera.LedIntensity);

    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "testCamera - securely ended");

    return success;
}

int CCamera::SetLEDIntensity(int _intrel)
{
    Camera.LedIntensity = (int)((float)(std::min(std::max(0, _intrel), 100)) / 100 * 8191);
    ESP_LOGD(TAG, "Set led_intensity to %i of 8191", Camera.LedIntensity);
    return Camera.LedIntensity;
}

bool CCamera::getCameraInitSuccessful(void)
{
    return Camera.CamInitSuccessful;
}

esp_err_t CCamera::SetSensorControllConfig(camera_controll_config_t *camConfig)
{
    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "SetSensorControllConfig - Start");

    sensor_t *sensor = esp_camera_sensor_get();
    if (sensor != NULL)
    {
        int ret = 0;
        ret = sensor->set_xclk(sensor, CAM_XCLK_TIMER, camConfig->CamXclkFreqMhz);

        ret |= sensor->set_framesize(sensor, camConfig->ImageFrameSize);

        ret |= sensor->set_saturation(sensor, camConfig->ImageSaturation); // -2 to 2
        ret |= sensor->set_contrast(sensor, camConfig->ImageContrast);     // -2 to 2
        ret |= sensor->set_brightness(sensor, camConfig->ImageBrightness); // -2 to 2

        ret |= sensor->set_gainceiling(sensor, (gainceiling_t)camConfig->ImageGainceiling); // Image gain (GAINCEILING_x2, x4, x8, x16, x32, x64 or x128)
        ret |= sensor->set_quality(sensor, camConfig->ImageQuality);                        // 0 - 63

        ret |= sensor->set_gain_ctrl(sensor, camConfig->ImageAgc);     // 0 = disable , 1 = enable
        ret |= sensor->set_exposure_ctrl(sensor, camConfig->ImageAec); // 0 = disable , 1 = enable
        ret |= sensor->set_hmirror(sensor, camConfig->ImageHmirror);   // 0 = disable , 1 = enable
        ret |= sensor->set_vflip(sensor, camConfig->ImageVflip);       // 0 = disable , 1 = enable

        ret |= sensor->set_whitebal(sensor, camConfig->ImageAwb);                 // 0 = disable , 1 = enable
        ret |= sensor->set_aec2(sensor, camConfig->ImageAec2);                    // 0 = disable , 1 = enable
        ret |= sensor->set_aec_value(sensor, camConfig->ImageAecValue);           // 0 to 1200
        ret |= sensor->set_special_effect(sensor, camConfig->ImageSpecialEffect); // 0 to 6 (0 - No Effect, 1 - Negative, 2 - Grayscale, 3 - Red Tint, 4 - Green Tint, 5 - Blue Tint, 6 - Sepia)
        ret |= sensor->set_wb_mode(sensor, camConfig->ImageWbMode);               // 0 to 4 - if awb_gain enabled (0 - Auto, 1 - Sunny, 2 - Cloudy, 3 - Office, 4 - Home)
        ret |= sensor->set_ae_level(sensor, camConfig->ImageAeLevel);             // -2 to 2

        ret |= sensor->set_dcw(sensor, camConfig->ImageDcw);          // 0 = disable , 1 = enable
        ret |= sensor->set_bpc(sensor, camConfig->ImageBpc);          // 0 = disable , 1 = enable
        ret |= sensor->set_wpc(sensor, camConfig->ImageWpc);          // 0 = disable , 1 = enable
        ret |= sensor->set_awb_gain(sensor, camConfig->ImageAwbGain); // 0 = disable , 1 = enable
        ret |= sensor->set_agc_gain(sensor, camConfig->ImageAgcGain); // 0 to 30

        ret |= sensor->set_raw_gma(sensor, camConfig->ImageRawGma); // 0 = disable , 1 = enable
        ret |= sensor->set_lenc(sensor, camConfig->ImageLenc);      // 0 = disable , 1 = enable

        // ret |= sensor->set_sharpness(sensor, camConfig->ImageSharpness);   // auto-sharpness is not officially supported, default to 0
        ret |= SetCamSharpness(sensor, camConfig->ImageAutoSharpness, camConfig->ImageSharpness);
        ret |= sensor->set_denoise(sensor, camConfig->ImageDenoiseLevel); // The OV2640 does not support it, OV3660 and OV5640 (0 to 8)

        vTaskDelay(500 / portTICK_PERIOD_MS);

        LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "SetSensorControllConfig - securely ended");

        return ret;
    }

    return ESP_FAIL;
}

esp_err_t CCamera::GetSensorControllConfig(camera_controll_config_t *camConfig)
{
    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "GetSensorControllConfig - Start");

    sensor_t *sensor = esp_camera_sensor_get();
    if (sensor != NULL)
    {
        Camera.CamSensor_id = sensor->id.PID;
        camConfig->CamXclkFreqMhz = (int)(sensor->xclk_freq_hz / 1000000);

        camConfig->ImageFrameSize = sensor->status.framesize;

        camConfig->ImageSaturation = (int)sensor->status.saturation;
        camConfig->ImageContrast = (int)sensor->status.contrast;
        camConfig->ImageBrightness = (int)sensor->status.brightness;

        camConfig->ImageGainceiling = (int)sensor->status.gainceiling;
        camConfig->ImageQuality = (int)sensor->status.quality;

        camConfig->ImageAgc = (int)sensor->status.agc;
        camConfig->ImageAec = (int)sensor->status.aec;
        camConfig->ImageHmirror = (int)sensor->status.hmirror;
        camConfig->ImageVflip = (int)sensor->status.vflip;

        camConfig->ImageAwb = (int)sensor->status.awb;
        camConfig->ImageAec2 = (int)sensor->status.aec2;
        camConfig->ImageAecValue = (int)sensor->status.aec_value;
        camConfig->ImageSpecialEffect = (int)sensor->status.special_effect;
        camConfig->ImageWbMode = (int)sensor->status.wb_mode;
        camConfig->ImageAeLevel = (int)sensor->status.ae_level;

        camConfig->ImageDcw = (int)sensor->status.dcw;
        camConfig->ImageBpc = (int)sensor->status.bpc;
        camConfig->ImageWpc = (int)sensor->status.wpc;
        camConfig->ImageAwbGain = (int)sensor->status.awb_gain;
        camConfig->ImageAgcGain = (int)sensor->status.agc_gain;

        camConfig->ImageRawGma = (int)sensor->status.raw_gma;
        camConfig->ImageLenc = (int)sensor->status.lenc;

        camConfig->ImageSharpness = (int)sensor->status.sharpness;
        camConfig->ImageDenoiseLevel = (int)sensor->status.denoise;

        LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "GetSensorControllConfig - securely ended");

        return ESP_OK;
    }

    return ESP_FAIL;
}

esp_err_t CCamera::SetCameraConfigFromTo(camera_controll_config_t *camConfigFrom, camera_controll_config_t *camConfigTo)
{
    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "SetCameraConfigFromTo - Start");

    camConfigTo->CamXclkFreqMhz = camConfigFrom->CamXclkFreqMhz;

    camConfigTo->ImageFrameSize = camConfigFrom->ImageFrameSize;

    camConfigTo->ImageSaturation = camConfigFrom->ImageSaturation;
    camConfigTo->ImageContrast = camConfigFrom->ImageContrast;
    camConfigTo->ImageBrightness = camConfigFrom->ImageBrightness;

    camConfigTo->ImageQuality = camConfigFrom->ImageQuality;

    camConfigTo->ImageGainceiling = camConfigFrom->ImageGainceiling;

    camConfigTo->ImageAgc = camConfigFrom->ImageAgc;
    camConfigTo->ImageAec = camConfigFrom->ImageAec;
    camConfigTo->ImageHmirror = camConfigFrom->ImageHmirror;
    camConfigTo->ImageVflip = camConfigFrom->ImageVflip;

    camConfigTo->ImageAwb = camConfigFrom->ImageAwb;
    camConfigTo->ImageAec2 = camConfigFrom->ImageAec2;
    camConfigTo->ImageAecValue = camConfigFrom->ImageAecValue;
    camConfigTo->ImageSpecialEffect = camConfigFrom->ImageSpecialEffect;
    camConfigTo->ImageWbMode = camConfigFrom->ImageWbMode;
    camConfigTo->ImageAeLevel = camConfigFrom->ImageAeLevel;

    camConfigTo->ImageDcw = camConfigFrom->ImageDcw;
    camConfigTo->ImageBpc = camConfigFrom->ImageBpc;
    camConfigTo->ImageWpc = camConfigFrom->ImageWpc;
    camConfigTo->ImageAwbGain = camConfigFrom->ImageAwbGain;
    camConfigTo->ImageAgcGain = camConfigFrom->ImageAgcGain;

    camConfigTo->ImageRawGma = camConfigFrom->ImageRawGma;
    camConfigTo->ImageLenc = camConfigFrom->ImageLenc;

    camConfigTo->ImageSharpness = camConfigFrom->ImageSharpness;
    camConfigTo->ImageAutoSharpness = camConfigFrom->ImageAutoSharpness;

    camConfigTo->ImageDenoiseLevel = camConfigFrom->ImageDenoiseLevel;

    camConfigTo->ImageZoomEnabled = camConfigFrom->ImageZoomEnabled;
    camConfigTo->ImageZoomOffsetX = camConfigFrom->ImageZoomOffsetX;
    camConfigTo->ImageZoomOffsetY = camConfigFrom->ImageZoomOffsetY;
    camConfigTo->ImageZoomSize = camConfigFrom->ImageZoomSize;

    camConfigTo->ImageNegative = camConfigFrom->ImageNegative;

    camConfigTo->ImageLedIntensity = camConfigFrom->ImageLedIntensity;
    camConfigTo->WaitBeforePicture = camConfigFrom->WaitBeforePicture;
    camConfigTo->ImageInitialRotate = camConfigFrom->ImageInitialRotate;

    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "SetCameraConfigFromTo - securely ended");

    return ESP_OK;
}

int CCamera::CheckCamSettingsChanged(void)
{
    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "CheckCamSettingsChanged - Start");

    int ret = 0;

    SetCamDeepSleep(false);

    // wenn die Kameraeinstellungen durch Erstellen eines neuen Referenzbildes verändert wurden, müssen sie neu gesetzt werden
    if (Camera.CamSettingsChanged)
    {
        if (Camera.CamTempImage)
        {
            Camera.SetSensorControllConfig(&CameraConfigTemp); // CameraConfigTemp >>> Kamera
            Camera.SetQualityZoomSize(&CameraConfigTemp);
            Camera.LedIntensity = CameraConfigTemp.ImageLedIntensity;
            Camera.CamTempImage = false;
        }
        else
        {
            Camera.SetSensorControllConfig(&CameraConfigSaved); // CameraConfigSaved >>> Kamera
            Camera.SetQualityZoomSize(&CameraConfigSaved);
            Camera.LedIntensity = CameraConfigSaved.ImageLedIntensity;
            Camera.CamSettingsChanged = false;
        }
    }

    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "CheckCamSettingsChanged - securely ended");

    return ret;
}

// only available on OV3660 and OV5640
// https://github.com/espressif/esp32-camera/issues/672
int CCamera::SetCamDeepSleep(bool enable)
{
    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "SetCamDeepSleep - Start");

    int ret = 0;
    if (Camera.CameraDeepSleepEnable != enable)
    {
        Camera.CameraDeepSleepEnable = enable;

        sensor_t *sensor = esp_camera_sensor_get();
        if (sensor != NULL)
        {
            std::string state = "unsupported";
            if (Camera.CamSensor_id == OV2640_PID)
            {
                // OV2640 Standby mode
                uint8_t reg = sensor->get_reg(sensor, 0x09, 0xFF);
                ret = sensor->set_reg(sensor, 0x09, 0xFF, enable ? (reg |= 0x10) : (reg &= ~0x10));
                state = enable ? "enabled" : "disabled";
            }
#if CONFIG_OV3660_SUPPORT || CONFIG_OV5640_SUPPORT
            else if ((Camera.CamSensor_id == OV3660_PID) || (Camera.CamSensor_id == OV5640_PID))
            {
                // OV3660/OV5640 DeepSleep mode
                ret = sensor->set_reg(sensor, 0x3008, 0x42, enable ? 0x42 : 0x02);
                state = enable ? "enabled" : "disabled";
            }
#endif
            LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "DeepSleep: " + state);
            vTaskDelay(200 / portTICK_PERIOD_MS);
        }
        else
        {
            return -1;
        }
    }

    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "SetCamDeepSleep - securely ended");

    return ret;
}

int CCamera::SetCamSharpness(sensor_t *sensor, bool autoSharpnessEnabled, int sharpnessLevel)
{
    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "SetCamSharpness - Start");

    int ret = 0;

    if (Camera.CamSensor_id == OV2640_PID)
    {
        // The OV2640 does not officially support sharpness
        if (autoSharpnessEnabled)
        {
            ret = sensor->set_sharpness(sensor, 4);
        }
        else
        {
            ret = sensor->set_sharpness(sensor, sharpnessLevel);
        }
    }
#if CONFIG_OV3660_SUPPORT || CONFIG_OV5640_SUPPORT
    else if ((Camera.CamSensor_id == OV3660_PID) || (Camera.CamSensor_id == OV5640_PID))
    {
        // for CAMERA_OV5640 and CAMERA_OV3660
        if (autoSharpnessEnabled)
        {
            // autoSharpness is not supported, default to zero
            ret = sensor->set_sharpness(sensor, 0);
        }
        else
        {
            ret = sensor->set_sharpness(sensor, sharpnessLevel);
        }
    }
#endif

    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "SetCamSharpness - securely ended");

    return ret;
}

// - It always zooms to the image center when offsets are zero
// - if imageSize = 0 then the image is not zoomed
// - if imageSize = max value, then the image is fully zoomed in
// - a zoom step is >>> Width + 32 px / Height + 24 px
void CCamera::SanitizeZoomParams(camera_controll_config_t *camConfig, int imageSize, int frameSizeX, int frameSizeY, int &imageWidth, int &imageHeight, int &zoomOffsetX, int &zoomOffsetY)
{
    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "SanitizeZoomParams - Start");

    // for OV2640, This works only if the aspect ratio of 4:3 is preserved in the window size.
    // use only values divisible by 8 without remainder
    imageWidth = camConfig->ImageWidth + (imageSize * 4 * 8);
    imageHeight = camConfig->ImageHeight + (imageSize * 3 * 8);

    int _maxX = frameSizeX - imageWidth;
    int _maxY = frameSizeY - imageHeight;

    if ((abs(zoomOffsetX) * 2) > _maxX)
    {
        if (zoomOffsetX > 0)
        {
            zoomOffsetX = _maxX;
        }
        else
        {
            zoomOffsetX = 0;
        }
    }
    else
    {
        if (zoomOffsetX > 0)
        {
            zoomOffsetX = ((_maxX / 2) + zoomOffsetX);
        }
        else
        {
            zoomOffsetX = ((_maxX / 2) + zoomOffsetX);
        }
    }

    if ((abs(zoomOffsetY) * 2) > _maxY)
    {
        if (zoomOffsetY > 0)
        {
            zoomOffsetY = _maxY;
        }
        else
        {
            zoomOffsetY = 0;
        }
    }
    else
    {
        if (zoomOffsetY > 0)
        {
            zoomOffsetY = ((_maxY / 2) + zoomOffsetY);
        }
        else
        {
            zoomOffsetY = ((_maxY / 2) + zoomOffsetY);
        }
    }

    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "SanitizeZoomParams - securely ended");
}

int CCamera::SetZoomSize(sensor_t *sensor, camera_controll_config_t *camConfig)
{
    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "SetZoomSize - Start");

    int ret = 0;

    if (camConfig->ImageZoomEnabled)
    {
        int _imageSize_temp = 0;

        int _imageWidth = camConfig->ImageWidth;
        int _imageHeight = camConfig->ImageHeight;
        int _offsetx = camConfig->ImageZoomOffsetX;
        int _offsety = camConfig->ImageZoomOffsetY;
        int frameSizeX = 1600;
        int frameSizeY = 1200;

        switch (Camera.CamSensor_id)
        {
        case OV2640_PID:
        {
            frameSizeX = 1600;
            frameSizeY = 1200;
            // max imageSize = ((frameSizeX - CameraConfigSaved.ImageWidth) / 8 / 4) -1
            // 29 = ((1600 - 640) / 8 / 4) - 1
            if (camConfig->ImageZoomSize < 29)
            {
                _imageSize_temp = (29 - camConfig->ImageZoomSize);
            }
        }
        break;
#if CONFIG_OV3660_SUPPORT
        case OV3660_PID:
        {
            frameSizeX = 2048;
            frameSizeY = 1536;
            // max imageSize = ((frameSizeX - CameraConfigSaved.ImageWidth) / 8 / 4) -1
            // 43 = ((2048 - 640) / 8 / 4) - 1
            if (camConfig->ImageZoomSize < 43)
            {
                _imageSize_temp = (43 - camConfig->ImageZoomSize);
            }
        }
        break;
#endif
#if CONFIG_OV5640_SUPPORT
        case OV5640_PID:
        {
            frameSizeX = 2592;
            frameSizeY = 1944;
            // max imageSize = ((frameSizeX - CameraConfigSaved.ImageWidth) / 8 / 4) - 1
            // 59 = ((2560 - 640) / 8 / 4) - 1
            if (camConfig->ImageZoomSize < 59)
            {
                _imageSize_temp = (59 - camConfig->ImageZoomSize);
            }
        }
        break;
#endif
        }

        SanitizeZoomParams(camConfig, _imageSize_temp, frameSizeX, frameSizeY, _imageWidth, _imageHeight, _offsetx, _offsety);
        ret = SetCamWindow(sensor, camConfig, frameSizeX, frameSizeY, _offsetx, _offsety, _imageWidth, _imageHeight);
    }
    else
    {
        ret = sensor->set_framesize(sensor, camConfig->ImageFrameSize);
    }

    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "SetZoomSize - securely ended");

    return ret;
}

int CCamera::SetQualityZoomSize(camera_controll_config_t *camConfig)
{
    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "SetQualityZoomSize - Start");

    int ret = 0;

    SetImageWidthHeightFromResolution(camConfig);

    sensor_t *sensor = esp_camera_sensor_get();
    if (sensor != NULL)
    {
        ret = sensor->set_quality(sensor, camConfig->ImageQuality);
        ret |= SetZoomSize(sensor, camConfig);
    }
    else
    {
        ret = -1;
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "SetQualityZoomSize, Failed to get Cam control structure");
    }

    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "SetQualityZoomSize - securely ended");

    return ret;
}

int CCamera::SetCamWindow(sensor_t *sensor, camera_controll_config_t *camConfig, int frameSizeX, int frameSizeY, int xOffset, int yOffset, int xTotal, int yTotal)
{
    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "SetCamWindow - Start");

    int ret = 0;

    int outputX = camConfig->ImageWidth;
    int outputY = camConfig->ImageHeight;

    if (Camera.CamSensor_id == OV2640_PID)
    {
        ret = sensor->set_res_raw(sensor, 0, 0, 0, 0, xOffset, yOffset, xTotal, yTotal, outputX, outputY, false, false);
    }
#if CONFIG_OV3660_SUPPORT || CONFIG_OV5640_SUPPORT
    else if ((Camera.CamSensor_id == OV3660_PID) || (Camera.CamSensor_id == OV5640_PID))
    {
        // for CAMERA_OV5640 and CAMERA_OV3660
        bool scale = !(outputX == xTotal && outputY == yTotal);
        bool binning = (xTotal >= (frameSizeX >> 1));

        if (camConfig->ImageVflip == true)
        {
            ret = sensor->set_res_raw(sensor, xOffset, yOffset, xOffset + xTotal - 1, yOffset + yTotal - 1, 0, 0, frameSizeX, frameSizeY, outputX, outputY, scale, binning);
        }
        else
        {
            ret = sensor->set_res_raw(sensor, xOffset, yOffset, xOffset + xTotal, yOffset + yTotal, 0, 0, frameSizeX, frameSizeY, outputX, outputY, scale, binning);
        }
    }
#endif

    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "SetCamWindow - securely ended");

    return ret;
}

// is used by:
// jomjol_flowcontroll/ClassFlowTakeImage.cpp - ImageData *ClassFlowTakeImage::SendRawImage(void) <- ?
// jomjol_flowcontroll/ClassFlowTakeImage.cpp - void ClassFlowTakeImage::takePictureWithFlash(int flash_duration) <- bool ClassFlowTakeImage::doFlow(std::string zwtime)
esp_err_t CCamera::CaptureToBasisImage(CImageBasis *_Image, int flash_duration)
{
    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "CaptureToBasisImage - Start");

    int _width = CameraConfigSaved.ImageWidth;
    int _height = CameraConfigSaved.ImageHeight;
    int _ImageQuality = CameraConfigSaved.ImageQuality;
    bool _ImageNegative = CameraConfigSaved.ImageNegative;
    int _ImageSpecialEffect = CameraConfigSaved.ImageSpecialEffect;
    if (Camera.CamTempImage)
    {
        _width = CameraConfigTemp.ImageWidth;
        _height = CameraConfigTemp.ImageHeight;
        _ImageQuality = CameraConfigTemp.ImageQuality;
        _ImageNegative = CameraConfigTemp.ImageNegative;
        _ImageSpecialEffect = CameraConfigTemp.ImageSpecialEffect;
    }

    int64_t fr_start = esp_timer_get_time();
    CheckCamSettingsChanged();

    camera_fb_t *fb = esp_camera_fb_get();
    esp_camera_fb_return(fb);

    ESP_LOGI(TAG, "CaptureToBasisImage - flash_duration: %dms", flash_duration);
    if (flash_duration > 0)
    {
        CaptureToBasisImageLed = true;
        FlashLightOnOff(true, Camera.LedIntensity); // Flash-LED on
        fb = esp_camera_fb_get();
        esp_camera_fb_return(fb);
        vTaskDelay(flash_duration / portTICK_PERIOD_MS);
    }
    else
    {
        StatusLEDOnOff(true); // Status-LED on
    }

    fb = esp_camera_fb_get();
    esp_camera_fb_return(fb);
    fb = esp_camera_fb_get();
    if (!fb)
    {
        if (flash_duration > 0)
        {
            CaptureToBasisImageLed = false;
            if (!CaptureToFileLed && !CaptureToHTTPLed && !CaptureToStreamLed)
            {
                FlashLightOnOff(false, Camera.LedIntensity); // Flash-LED off
            }
        }
        else
        {
            StatusLEDOnOff(false); // Status-LED off
        }

        LogFile.WriteToFile(ESP_LOG_ERROR, TAG,
                            "an error occurred (CaptureToBasisImage) - most probably caused "
                            "by a hardware problem (instablility, ...).");

        esp_camera_fb_return(fb);
        SetCamDeepSleep(true);
        return ESP_FAIL;
    }

    if (Camera.DemoMode)
    {
        SetCamDeepSleep(true);
        // Use images stored on SD-Card instead of camera image
        /* Replace Framebuffer with image from SD-Card */
        loadNextDemoImage(fb);
    }

    uint8_t *buf = NULL;
    size_t buf_len = 0;
    bool converted = false;

    CImageBasis *TempImage = new CImageBasis("TempImage");
    if (TempImage)
    {
        if (fb->format != PIXFORMAT_JPEG)
        {
            if (!frame2jpg(fb, (100 - _ImageQuality), &buf, &buf_len))
            {
                ESP_LOGE(TAG, "JPEG compression failed");
            }
            converted = true;
            TempImage->LoadFromMemory(buf, buf_len);
        }
        else
        {
            TempImage->LoadFromMemory(fb->buf, fb->len);
        }
    }
    else
    {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "CaptureToBasisImage: Can't allocate TempImage");

        if (flash_duration > 0)
        {
            CaptureToBasisImageLed = false;
            if (!CaptureToFileLed && !CaptureToHTTPLed && !CaptureToStreamLed)
            {
                FlashLightOnOff(false, Camera.LedIntensity); // Flash-LED off
            }
        }
        else
        {
            StatusLEDOnOff(false); // Status-LED off
        }

        esp_camera_fb_return(fb);
        // esp_camera_return_all();
        free(buf);

        delete (TempImage);

        SetCamDeepSleep(true);
        int64_t fr_end = esp_timer_get_time();
        ESP_LOGI(TAG, "CaptureToBasisImage: %dms", (int)((fr_end - fr_start) / 1000));

        return ESP_FAIL;
    }

    if (flash_duration > 0)
    {
        CaptureToBasisImageLed = false;
        if (!CaptureToFileLed && !CaptureToHTTPLed && !CaptureToStreamLed)
        {
            FlashLightOnOff(false, Camera.LedIntensity); // Flash-LED off
        }
    }
    else
    {
        StatusLEDOnOff(false); // Status-LED off
    }

    esp_camera_fb_return(fb);
    // esp_camera_return_all();

    if (converted)
    {
        free(buf);
    }

    SetCamDeepSleep(true);
    int64_t fr_end = esp_timer_get_time();
    ESP_LOGI(TAG, "CaptureToBasisImage: %dms", (int)((fr_end - fr_start) / 1000));

    if (_ImageNegative && _ImageSpecialEffect != 1)
    {
        TempImage->setImageNegative();
    }

    _Image->EmptyImage(); // Delete previous stored raw image -> black image

    stbi_uc *p_target;
    stbi_uc *p_source;
    int channels = 3;

    for (int x = 0; x < _width; ++x)
    {
        for (int y = 0; y < _height; ++y)
        {
            p_target = _Image->rgb_image + (channels * (y * _width + x));
            p_source = TempImage->rgb_image + (channels * (y * _width + x));

            for (int c = 0; c < channels; c++)
            {
                p_target[c] = p_source[c];
            }
        }
    }

    delete (TempImage);

    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "CaptureToBasisImage - securely ended");

    return ESP_OK;
}

// is used by:
// jomjol_controlcamera/server_camera.cpp - esp_err_t handler_capture_save_to_file(httpd_req_t *req) <- camuri.uri = "/save";
// jomjol_flowcontroll/ClassFlowTakeImage.cpp - esp_err_t ClassFlowTakeImage::camera_capture(void) <- ?
esp_err_t CCamera::CaptureToFile(std::string pathImage, int flash_duration)
{
    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "CaptureToFile - Start");

    int _ImageQuality = CameraConfigSaved.ImageQuality;
    if (Camera.CamTempImage)
    {
        _ImageQuality = CameraConfigTemp.ImageQuality;
    }

    int64_t fr_start = esp_timer_get_time();
    CheckCamSettingsChanged();

    camera_fb_t *fb = esp_camera_fb_get();
    esp_camera_fb_return(fb);

    ESP_LOGI(TAG, "CaptureToFile - flash_duration: %dms", flash_duration);
    if (flash_duration > 0)
    {
        CaptureToFileLed = true;
        FlashLightOnOff(true, Camera.LedIntensity); // Flash-LED on
        fb = esp_camera_fb_get();
        esp_camera_fb_return(fb);
        vTaskDelay(flash_duration / portTICK_PERIOD_MS);
    }
    else
    {
        StatusLEDOnOff(true); // Status-LED on
    }

    fb = esp_camera_fb_get();
    esp_camera_fb_return(fb);
    fb = esp_camera_fb_get();
    if (!fb)
    {
        if (flash_duration > 0)
        {
            CaptureToFileLed = false;
            if (!CaptureToBasisImageLed && !CaptureToHTTPLed && !CaptureToStreamLed)
            {
                FlashLightOnOff(false, Camera.LedIntensity); // Flash-LED off
            }
        }
        else
        {
            StatusLEDOnOff(false); // Status-LED off
        }

        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "CaptureToFile: Capture Failed. Check camera module and/or proper electrical connection.");
        esp_camera_fb_return(fb);
        SetCamDeepSleep(true);
        return ESP_FAIL;
    }

    pathImage = FormatFileName(pathImage);
    std::string ftype = toUpper(getFileType(pathImage));

    uint8_t *buf = NULL;
    size_t buf_len = 0;
    bool converted = false;

    if (ftype.compare("BMP") == 0)
    {
        if (!frame2bmp(fb, &buf, &buf_len))
        {
            ESP_LOGE(TAG, "BMP compression failed");
        }
        converted = true;
    }
    else
    {
        if (fb->format != PIXFORMAT_JPEG)
        {
            if (!frame2jpg(fb, (100 - _ImageQuality), &buf, &buf_len))
            {
                ESP_LOGE(TAG, "JPEG compression failed");
            }
            converted = true;
        }
        else
        {
            buf_len = fb->len;
            buf = fb->buf;
        }
    }

    if (flash_duration > 0)
    {
        CaptureToFileLed = false;
        if (!CaptureToBasisImageLed && !CaptureToHTTPLed && !CaptureToStreamLed)
        {
            FlashLightOnOff(false, Camera.LedIntensity); // Flash-LED off
        }
    }
    else
    {
        StatusLEDOnOff(false); // Status-LED off
    }

    esp_camera_fb_return(fb);

    SetCamDeepSleep(true);
    int64_t fr_end = esp_timer_get_time();
    ESP_LOGI(TAG, "CaptureToFile: %dms", (int)((fr_end - fr_start) / 1000));

    FILE *filep = fopen(pathImage.c_str(), "wb");
    if (filep == NULL)
    {
        // If an error occurs during the file creation
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "CaptureToFile: Failed to open file " + pathImage);
    }
    else
    {
        fwrite(buf, sizeof(uint8_t), buf_len, filep);
        fclose(filep);
    }

    if (converted)
    {
        free(buf);
    }

    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "CaptureToFile - securely ended");

    return ESP_OK;
}

// is used by:
// jomjol_controlcamera/server_camera.cpp - esp_err_t handler_capture(httpd_req_t *req) <- camuri.uri = "/capture";
// jomjol_controlcamera/server_camera.cpp - esp_err_t handler_capture_with_light(httpd_req_t *req) <- camuri.uri = "/capture_with_flashlight";
// jomjol_flowcontroll/ClassFlowTakeImage.cpp - esp_err_t ClassFlowTakeImage::SendRawJPG(httpd_req_t *req) <- jomjol_flowcontroll/ClassFlowControll.cpp - esp_err_t ClassFlowControll::SendRawJPG(httpd_req_t *req) <- jomjol_flowcontroll/MainFlowControl.cpp
// - esp_err_t GetRawJPG(httpd_req_t *req) <- jomjol_network/server_main.cpp - esp_err_t img_tmp_virtual_handler(httpd_req_t *req) <- .uri = "/img_tmp/*";
esp_err_t CCamera::CaptureToHTTP(httpd_req_t *req, int flash_duration)
{
    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "CaptureToHTTP - Start");

    int _ImageQuality = CameraConfigSaved.ImageQuality;
    if (Camera.CamTempImage)
    {
        _ImageQuality = CameraConfigTemp.ImageQuality;
    }

    int64_t fr_start = esp_timer_get_time();
    CheckCamSettingsChanged();

    camera_fb_t *fb = esp_camera_fb_get();
    esp_camera_fb_return(fb);

    ESP_LOGI(TAG, "CaptureToHTTP - flash_duration: %dms", flash_duration);
    if (flash_duration > 0)
    {
        CaptureToHTTPLed = true;
        FlashLightOnOff(true, Camera.LedIntensity); // Flash-LED on
        fb = esp_camera_fb_get();
        esp_camera_fb_return(fb);
        vTaskDelay(flash_duration / portTICK_PERIOD_MS);
    }
    else
    {
        StatusLEDOnOff(true); // Status-LED on
    }

    fb = esp_camera_fb_get();
    esp_camera_fb_return(fb);
    fb = esp_camera_fb_get();
    if (!fb)
    {
        if (flash_duration > 0)
        {
            CaptureToHTTPLed = false;
            if (!CaptureToBasisImageLed && !CaptureToFileLed && !CaptureToStreamLed)
            {
                FlashLightOnOff(false, Camera.LedIntensity); // Flash-LED off
            }
        }
        else
        {
            StatusLEDOnOff(false); // Status-LED off
        }

        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "CaptureToHTTP: Capture Failed. Check camera module and/or proper electrical connection.");
        httpd_resp_send_500(req);
        httpd_resp_send_chunk(req, NULL, 0);

        esp_camera_fb_return(fb);
        SetCamDeepSleep(true);
        return ESP_FAIL;
    }

    esp_err_t res = httpd_resp_set_type(req, "image/jpeg");
    if (res == ESP_OK)
    {
        res = httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=raw.jpg");
    }

    uint8_t *buf = NULL;
    size_t buf_len = 0;
    bool converted = false;

    if (res == ESP_OK)
    {
        if (Camera.DemoMode)
        {
            // Use images stored on SD-Card instead of camera image
            LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "Using Demo image!");
            /* Replace Framebuffer with image from SD-Card */
            loadNextDemoImage(fb);

            res = httpd_resp_send(req, (const char *)fb->buf, fb->len);
            httpd_resp_send_chunk(req, NULL, 0);
        }
        else
        {
            if (fb->format != PIXFORMAT_JPEG)
            {
                buf_len = fb->len;
                if (!frame2jpg(fb, (100 - _ImageQuality), &buf, &buf_len))
                {
                    ESP_LOGE(TAG, "JPEG compression failed");
                }
                converted = true;
                res = httpd_resp_send(req, (const char *)buf, buf_len);
                httpd_resp_send_chunk(req, NULL, 0);
            }
            else
            {
                buf_len = fb->len;
                res = httpd_resp_send(req, (const char *)fb->buf, fb->len);
                httpd_resp_send_chunk(req, NULL, 0);
            }
        }
    }

    if (flash_duration > 0)
    {
        CaptureToHTTPLed = false;
        if (!CaptureToBasisImageLed && !CaptureToFileLed && !CaptureToStreamLed)
        {
            FlashLightOnOff(false, Camera.LedIntensity); // Flash-LED off
        }
    }
    else
    {
        StatusLEDOnOff(false); // Status-LED off
    }

    esp_camera_fb_return(fb);

    if (converted)
    {
        free(buf);
    }

    SetCamDeepSleep(true);
    int64_t fr_end = esp_timer_get_time();
    ESP_LOGI(TAG, "CaptureToHTTP: %dKB %dms", (int)(buf_len / 1024), (int)((fr_end - fr_start) / 1000));

    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "CaptureToHTTP - securely ended");

    return res;
}

// is used by:
// jomjol_flowcontroll/MainFlowControl.cpp <- esp_err_t handler_stream(httpd_req_t *req) <- camuri.uri = "/stream";
esp_err_t CCamera::CaptureToStream(httpd_req_t *req, bool flashlightOn)
{
    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "Live stream started");

    int _ImageQuality = CameraConfigSaved.ImageQuality;
    if (Camera.CamTempImage)
    {
        _ImageQuality = CameraConfigTemp.ImageQuality;
    }

    int64_t fr_start = esp_timer_get_time();
    CheckCamSettingsChanged();

    camera_fb_t *fb = esp_camera_fb_get();
    esp_camera_fb_return(fb);

    if (flashlightOn)
    {
        CaptureToStreamLed = true;
        FlashLightOnOff(true, Camera.LedIntensity); // Flash-LED on
    }
    else
    {
        StatusLEDOnOff(true); // Status-LED on
    }

    esp_err_t res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
    res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));

    uint8_t *buf = NULL;
    size_t buf_len = 0;

    while (1)
    {
        int64_t refresh_start = esp_timer_get_time();
        buf_len = 0;

        fb = esp_camera_fb_get();
        if (!fb)
        {
            LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "CaptureToStream: Camera framebuffer not available");
            esp_camera_fb_return(fb);
            break;
        }

        bool converted = false;
        if (fb->format != PIXFORMAT_JPEG)
        {
            if (!frame2jpg(fb, (100 - _ImageQuality), &buf, &buf_len))
            {
                ESP_LOGE(TAG, "JPEG compression failed");
            }
            converted = true;
        }
        else
        {
            buf_len = fb->len;
        }

        if (res == ESP_OK)
        {
            char *part_buf[64];
            size_t hlen = snprintf((char *)part_buf, sizeof(part_buf), _STREAM_PART, buf_len);
            res = httpd_resp_send_chunk(req, (const char *)part_buf, hlen);
        }

        if (res == ESP_OK)
        {
            if (converted)
            {
                res = httpd_resp_send_chunk(req, (const char *)buf, buf_len);
            }
            else
            {
                res = httpd_resp_send_chunk(req, (const char *)fb->buf, fb->len);
            }
        }

        if (res == ESP_OK)
        {
            res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
        }

        esp_camera_fb_return(fb);

        if (converted)
        {
            free(buf);
        }

        if (res != ESP_OK)
        {
            // Exit loop, e.g. also when closing the webpage
            break;
        }

        int64_t refresh_end = esp_timer_get_time();
        int64_t refresh_delta_ms = (refresh_end - refresh_start) / 1000;

        if (CAM_LIVESTREAM_REFRESHRATE > refresh_delta_ms)
        {
            const TickType_t xDelay = (CAM_LIVESTREAM_REFRESHRATE - refresh_delta_ms) / portTICK_PERIOD_MS;
            ESP_LOGD(TAG, "Stream: sleep for: %ldms", (long)xDelay * 10);
            vTaskDelay(xDelay);
        }
    }

    if (flashlightOn)
    {
        CaptureToStreamLed = false;
        if (!CaptureToBasisImageLed && !CaptureToFileLed && !CaptureToHTTPLed)
        {
            FlashLightOnOff(false, Camera.LedIntensity); // Flash-LED off
        }
    }
    else
    {
        StatusLEDOnOff(false); // Status-LED off
    }

    SetCamDeepSleep(true);
    int64_t fr_end = esp_timer_get_time();
    ESP_LOGI(TAG, "CaptureToStream: %dms", (int)((fr_end - fr_start) / 1000));

    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "Live stream stopped");

    return res;
}

void CCamera::FlashLightOnOff(bool status, int intensity)
{
    GpioHandler *gpioHandler = gpio_handler_get();

    if (gpioHandler != NULL)
    {
        ESP_LOGD(TAG, "Use gpioHandler to trigger flashlight");
        gpioHandler->flashLightControl(status, intensity);
    }
}

void CCamera::StatusLEDOnOff(bool status)
{
    if (BLINK_GPIO != GPIO_NUM_NC)
    {
        if (xHandle_task_status_led == NULL)
        {
            // Init the GPIO
            gpio_pad_select_gpio(BLINK_GPIO);

            /* Set the GPIO as a push/pull output */
            gpio_set_direction(BLINK_GPIO, GPIO_MODE_OUTPUT);

#ifdef BLINK_GPIO_INVERT
            if (status)
            {
                gpio_set_level(BLINK_GPIO, 0);
            }
            else
            {
                gpio_set_level(BLINK_GPIO, 1);
            }
#else
            if (status)
            {
                gpio_set_level(BLINK_GPIO, 1);
            }
            else
            {
                gpio_set_level(BLINK_GPIO, 0);
            }
#endif
        }
    }
}

void CCamera::SetImageWidthHeightFromResolution(camera_controll_config_t *camConfig)
{
    if (camConfig->ImageFrameSize == FRAMESIZE_QVGA)
    {
        camConfig->ImageWidth = 320;
        camConfig->ImageHeight = 240;
        return;
    }
    else if (camConfig->ImageFrameSize == FRAMESIZE_VGA)
    {
        camConfig->ImageWidth = 640;
        camConfig->ImageHeight = 480;
        return;
    }
    else if (camConfig->ImageFrameSize == FRAMESIZE_SVGA)
    {
        camConfig->ImageWidth = 800;
        camConfig->ImageHeight = 600;
        return;
    }
    else if (camConfig->ImageFrameSize == FRAMESIZE_XGA)
    {
        camConfig->ImageWidth = 1024;
        camConfig->ImageHeight = 768;
        return;
    }
    else if (camConfig->ImageFrameSize == FRAMESIZE_HD)
    {
        camConfig->ImageWidth = 1280;
        camConfig->ImageHeight = 720;
        return;
    }
    else if (camConfig->ImageFrameSize == FRAMESIZE_SXGA)
    {
        camConfig->ImageWidth = 1280;
        camConfig->ImageHeight = 1024;
        return;
    }
    else if (camConfig->ImageFrameSize == FRAMESIZE_UXGA)
    {
        camConfig->ImageWidth = 1600;
        camConfig->ImageHeight = 1200;
        return;
    }
    else if (camConfig->ImageFrameSize == FRAMESIZE_QXGA)
    {
        camConfig->ImageWidth = 2048;
        camConfig->ImageHeight = 1536;
        return;
    }
    else if (camConfig->ImageFrameSize == FRAMESIZE_WQXGA)
    {
        camConfig->ImageWidth = 2560;
        camConfig->ImageHeight = 1600;
        return;
    }
    else if (camConfig->ImageFrameSize == FRAMESIZE_QSXGA)
    {
        camConfig->ImageWidth = 2560;
        camConfig->ImageHeight = 1920;
        return;
    }
    else
    {
        camConfig->ImageWidth = 640;
        camConfig->ImageHeight = 480;
        return;
    }
}

framesize_t CCamera::TextToFramesize(const char *_size)
{
    if (strcmp(_size, "QVGA") == 0)
    {
        return FRAMESIZE_QVGA; // 320x240
    }
    else if (strcmp(_size, "VGA") == 0)
    {
        return FRAMESIZE_VGA; // 640x480
    }
    else if (strcmp(_size, "SVGA") == 0)
    {
        return FRAMESIZE_SVGA; // 800x600
    }
    else if (strcmp(_size, "XGA") == 0)
    {
        return FRAMESIZE_XGA; // 1024x768
    }
    else if (strcmp(_size, "SXGA") == 0)
    {
        return FRAMESIZE_SXGA; // 1280x1024
    }
    else if (strcmp(_size, "UXGA") == 0)
    {
        return FRAMESIZE_UXGA; // 1600x1200
    }
    else if (strcmp(_size, "QXGA") == 0)
    {
        return FRAMESIZE_QXGA; // 2048x1536
    }
    else if (strcmp(_size, "WQXGA") == 0)
    {
        return FRAMESIZE_WQXGA; // 2560x1600
    }
    else if (strcmp(_size, "QSXGA") == 0)
    {
        return FRAMESIZE_QSXGA; // 2560x1920
    }
    else
    {
        return FRAMESIZE_VGA; // 640x480
    }
}

void CCamera::useDemoMode(void)
{
    char line[50];

    FILE *fd = fopen("/sdcard/demo/files.txt", "r");
    if (!fd)
    {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Can not start Demo mode, the folder '/sdcard/demo/' does not contain the needed files!");
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "See Details on https://jomjol.github.io/AI-on-the-edge-device-docs/Demo-Mode!");
        return;
    }

    tempImage = (uint8_t *)malloc(MAX_JPG_SIZE);
    if (tempImage == NULL)
    {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Unable to acquire required memory for demo image!");
        return;
    }

    while (fgets(line, sizeof(line), fd) != NULL)
    {
        line[strlen(line) - 1] = '\0';
        demoFiles.push_back(line);
    }

    fclose(fd);

    LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Using Demo mode (" + std::to_string(demoFiles.size()) + " files) instead of real camera image!");

    for (auto file : demoFiles)
    {
        LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, file);
    }
}

bool CCamera::loadNextDemoImage(camera_fb_t *fb)
{
    char filename[50];
    int readBytes;
    long fileSize;

    snprintf(filename, sizeof(filename), "/sdcard/demo/%s", demoFiles[getCountFlowRounds() % demoFiles.size()].c_str());

    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "Using " + std::string(filename) + " as demo image");

    /* Inject saved image */
    FILE *fp = fopen(filename, "rb");
    if (!fp)
    {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Failed to read file: " + std::string(filename) + "!");
        return false;
    }

    fileSize = GetFileSize(filename);
    if (fileSize > MAX_JPG_SIZE)
    {
        char buf[100];
        snprintf(buf, sizeof(buf), "Demo Image (%d bytes) is larger than provided buffer (%d bytes)!", (int)fileSize, MAX_JPG_SIZE);
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, std::string(buf));
        return false;
    }

    readBytes = fread(tempImage, 1, MAX_JPG_SIZE, fp);
    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "read " + std::to_string(readBytes) + " bytes");
    fclose(fp);

    fb->buf = tempImage; // Update pointer
    fb->len = readBytes;
    // ToDo do we also need to set height, width, format and timestamp?

    return true;
}

long CCamera::GetFileSize(std::string filename)
{
    struct stat stat_buf;
    long rc = stat(filename.c_str(), &stat_buf);
    return rc == 0 ? stat_buf.st_size : -1;
}
