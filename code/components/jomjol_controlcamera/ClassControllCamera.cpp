#include "ClassControllCamera.h"
#include "ClassLogFile.h"

#include <stdio.h>
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"

#include "Helper.h"
#include "statusled.h"
#include "CImageBasis.h"

#include "server_ota.h"
#include "server_GpioHandler.h"

#include "defines.h"

#include <esp_event.h>
#include <esp_log.h>
#include <esp_system.h>
#include <nvs_flash.h>
#include <sys/param.h>
#include <string.h>
#include <sys/stat.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_camera.h"

#include "driver/ledc.h"
#include "MainFlowControl.h"

#include "ov2640_sharpness.h"
#include "ov2640_specialEffect.h"
#include "ov2640_contrast_brightness.h"

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

CCamera Camera;
camera_controll_config_temp_t CCstatus;
camera_controll_config_temp_t CFstatus;

static const char *TAG = "CAMERA";

/* Camera live stream */
#define PART_BOUNDARY "123456789000000000000987654321"
static const char *_STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char *_STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char *_STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

uint8_t *demoImage = NULL;    // Buffer holding the demo image in bytes
#define DEMO_IMAGE_SIZE 30000 // Max size of demo image in bytes

// Camera module bus communications frequency.
// Originally: config.xclk_freq_mhz = 20000000, but this lead to visual artifacts on many modules.
// See https://github.com/espressif/esp32-camera/issues/150#issuecomment-726473652 et al.
#if !defined(XCLK_FREQ_MHZ)
// int xclk = 8;
int xclk = 16; // Orginal value
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

    .xclk_freq_hz = xclk * 1000000,
    .ledc_timer = CAM_XCLK_TIMER,     // LEDC timer to be used for generating XCLK
    .ledc_channel = CAM_XCLK_CHANNEL, // LEDC channel to be used for generating XCLK

    .pixel_format = PIXFORMAT_JPEG, // YUV422,GRAYSCALE,RGB565,JPEG
    .frame_size = FRAMESIZE_VGA,    // QQVGA-UXGA Do not use sizes above QVGA when not JPEG
    // .frame_size = FRAMESIZE_UXGA,    //QQVGA-UXGA Do not use sizes above QVGA when not JPEG
    .jpeg_quality = 12,                // 0-63 lower number means higher quality
    .fb_count = 1,                     // if more than one, i2s runs in continuous mode. Use only with JPEG
    .fb_location = CAMERA_FB_IN_PSRAM, /*!< The location where the frame buffer will be allocated */
    .grab_mode = CAMERA_GRAB_LATEST,   // only from new esp32cam version
};

typedef struct
{
    httpd_req_t *req;
    size_t len;
} jpg_chunking_t;

CCamera::CCamera(void)
{
    CCstatus.WaitBeforePicture = 2;

    ledc_init();
}

esp_err_t CCamera::InitCam(void)
{
    ESP_LOGD(TAG, "Init Camera");

    // initialize the camera
    esp_err_t err = esp_camera_init(&camera_config);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Camera Init Failed");
        return err;
    }

    CameraInitSuccessful = true;

    // Get a reference to the sensor
    // sensor_t cam_sensor = esp_camera_sensor_get();
    cam_sensor = esp_camera_sensor_get();
    if (cam_sensor != NULL)
    {
        // Camera.get_sensor_controll_config(&CCstatus); // Kamera >>> CCstatus
        CamSensorId = cam_sensor->id.PID;

        // Dump camera module, warn for unsupported modules.
        switch (CamSensorId)
        {
        case OV2640_PID:
            ESP_LOGI(TAG, "OV2640 camera module detected");
            break;
        case OV3660_PID:
            ESP_LOGI(TAG, "OV3660 camera module detected");
            break;
        case OV5640_PID:
            ESP_LOGI(TAG, "OV5640 camera module detected");
            break;
        default:
            ESP_LOGE(TAG, "Camera module is unknown and not properly supported!");
            CameraInitSuccessful = false;
        }
    }

    if (CameraInitSuccessful)
    {
        return ESP_OK;
    }
    else
    {
        return ESP_FAIL;
    }
}

void CCamera::ledc_init(void)
{
#ifdef USE_PWM_LEDFLASH
    // Prepare and then apply the LEDC PWM timer configuration
    ledc_timer_config_t ledc_timer = {};

    ledc_timer.speed_mode = LEDC_MODE;
    ledc_timer.timer_num = LEDC_TIMER;
    ledc_timer.duty_resolution = LEDC_DUTY_RES;
    ledc_timer.freq_hz = LEDC_FREQUENCY; // Set output frequency at 5 kHz
    ledc_timer.clk_cfg = LEDC_AUTO_CLK;

    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    // Prepare and then apply the LEDC PWM channel configuration
    ledc_channel_config_t ledc_channel = {};

    ledc_channel.speed_mode = LEDC_MODE;
    ledc_channel.channel = LEDC_CHANNEL;
    ledc_channel.timer_sel = LEDC_TIMER;
    ledc_channel.intr_type = LEDC_INTR_DISABLE;
    ledc_channel.gpio_num = LEDC_OUTPUT_IO;
    ledc_channel.duty = 0; // Set duty to 0%
    ledc_channel.hpoint = 0;
    // ledc_channel.flags.output_invert = LEDC_OUTPUT_INVERT;

    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));
#endif
}

int CCamera::SetLEDIntensity(int _intrel)
{
    // CCstatus.ImageLedIntensity = (int)(std::min(std::max((float)0, _intrel), (float)100) / 100 * 8191)
    Camera.LedIntensity = (int)((float)(std::min(std::max(0, _intrel), 100)) / 100 * 8191);
    ESP_LOGD(TAG, "Set led_intensity to %i of 8191", Camera.LedIntensity);
    return Camera.LedIntensity;
}

bool CCamera::getCameraInitSuccessful(void)
{
    return CameraInitSuccessful;
}

esp_err_t CCamera::set_sensor_controll_config(camera_controll_config_temp_t *camConfig)
{
    // sensor_t *cam_sensor = esp_camera_sensor_get();
    if (cam_sensor != NULL)
    {
        cam_sensor->set_xclk(cam_sensor, CAM_XCLK_TIMER, camConfig->CamXclkFreqMhz);

        cam_sensor->set_framesize(cam_sensor, camConfig->ImageFrameSize);

        // cam_sensor->set_contrast(cam_sensor, camConfig->ImageContrast);     // -2 to 2
        // cam_sensor->set_brightness(cam_sensor, camConfig->ImageBrightness); // -2 to 2
        set_camera_contrast_brightness(camConfig->ImageContrast, camConfig->ImageBrightness);

        cam_sensor->set_saturation(cam_sensor, camConfig->ImageSaturation); // -2 to 2

        cam_sensor->set_quality(cam_sensor, camConfig->ImageQuality); // 0 - 63

        // cam_sensor->set_gainceiling(cam_sensor, camConfig->ImageGainceiling); // Image gain (GAINCEILING_x2, x4, x8, x16, x32, x64 or x128)
        set_camera_gainceiling(camConfig->ImageGainceiling);

        cam_sensor->set_gain_ctrl(cam_sensor, camConfig->ImageAgc);     // 0 = disable , 1 = enable
        cam_sensor->set_exposure_ctrl(cam_sensor, camConfig->ImageAec); // 0 = disable , 1 = enable
        cam_sensor->set_hmirror(cam_sensor, camConfig->ImageHmirror);   // 0 = disable , 1 = enable
        cam_sensor->set_vflip(cam_sensor, camConfig->ImageVflip);       // 0 = disable , 1 = enable

        cam_sensor->set_whitebal(cam_sensor, camConfig->ImageAwb);       // 0 = disable , 1 = enable
        cam_sensor->set_aec2(cam_sensor, camConfig->ImageAec2);          // 0 = disable , 1 = enable
        cam_sensor->set_aec_value(cam_sensor, camConfig->ImageAecValue); // 0 to 1200
        // cam_sensor->set_special_effect(cam_sensor, camConfig->ImageSpecialEffect); // 0 to 6 (0 - No Effect, 1 - Negative, 2 - Grayscale, 3 - Red Tint, 4 - Green Tint, 5 - Blue Tint, 6 - Sepia)
        set_camera_special_effect(camConfig->ImageSpecialEffect);
        cam_sensor->set_wb_mode(cam_sensor, camConfig->ImageWbMode);   // 0 to 4 - if awb_gain enabled (0 - Auto, 1 - Sunny, 2 - Cloudy, 3 - Office, 4 - Home)
        cam_sensor->set_ae_level(cam_sensor, camConfig->ImageAeLevel); // -2 to 2

        cam_sensor->set_dcw(cam_sensor, camConfig->ImageDcw);          // 0 = disable , 1 = enable
        cam_sensor->set_bpc(cam_sensor, camConfig->ImageBpc);          // 0 = disable , 1 = enable
        cam_sensor->set_wpc(cam_sensor, camConfig->ImageWpc);          // 0 = disable , 1 = enable
        cam_sensor->set_awb_gain(cam_sensor, camConfig->ImageAwbGain); // 0 = disable , 1 = enable
        cam_sensor->set_agc_gain(cam_sensor, camConfig->ImageAgcGain); // 0 to 30

        cam_sensor->set_raw_gma(cam_sensor, camConfig->ImageRawGma); // 0 = disable , 1 = enable
        cam_sensor->set_lenc(cam_sensor, camConfig->ImageLenc);      // 0 = disable , 1 = enable

        // cam_sensor->set_sharpness(cam_sensor, camConfig->ImageSharpness);   // auto-sharpness is not officially supported, default to 0
        set_camera_sharpness(camConfig->ImageAutoSharpness, camConfig->ImageSharpness);
        cam_sensor->set_denoise(cam_sensor, camConfig->ImageDenoiseLevel); // The OV2640 does not support it, OV3660 and OV5640 (0 to 8)

        vTaskDelay(500 / portTICK_PERIOD_MS);

        return ESP_OK;
    }
    else
    {
        return ESP_FAIL;
    }
}

esp_err_t CCamera::get_sensor_controll_config(camera_controll_config_temp_t *camConfig)
{
    // sensor_t *cam_sensor = esp_camera_sensor_get();
    if (cam_sensor != NULL)
    {
        // Since there are always errors when reading out the parameters
        // and therefore problems when initializing the camera with the wrong parameters,
        // the parameters are checked and, if necessary, set with a default value.
        // note: The read value is usually "uint8_t" (see: https://github.com/espressif/esp32-camera/blob/dba8da9898928d9808d57a0b0cdcde9f130ed8fe/driver/include/sensor.h#L174#L203)
        //       However, the one to be set is usually "int" (see: https://github.com/espressif/esp32-camera/blob/dba8da9898928d9808d57a0b0cdcde9f130ed8fe/driver/include/sensor.h#L206#L253)
        CamSensorId = cam_sensor->id.PID;
        camConfig->CamXclkFreqMhz = get_value_or_default_int((int)(cam_sensor->xclk_freq_hz / 1000000), 20, 1, 16);

        camConfig->ImageFrameSize = (framesize_t)get_value_or_default_uint((int)cam_sensor->status.framesize, 10, 10);

        camConfig->ImageContrast = get_value_or_default_int((int)cam_sensor->status.contrast, 2, -2, 0);
        camConfig->ImageBrightness = get_value_or_default_int((int)cam_sensor->status.brightness, 2, -2, 0);
        camConfig->ImageSaturation = get_value_or_default_int((int)cam_sensor->status.saturation, 2, -2, 0);

        camConfig->ImageQuality = get_value_or_default_int((int)cam_sensor->status.quality, 63, 6, 12);

        camConfig->ImageGainceiling = (gainceiling_t)get_value_or_default_uint((int)cam_sensor->status.gainceiling, 6, 1);

        camConfig->ImageAgc = get_value_or_default_uint((int)cam_sensor->status.agc, 1, 1);
        camConfig->ImageAec = get_value_or_default_uint((int)cam_sensor->status.aec, 1, 1);
        camConfig->ImageHmirror = get_value_or_default_uint((int)cam_sensor->status.hmirror, 1, 0);
        camConfig->ImageVflip = get_value_or_default_uint((int)cam_sensor->status.vflip, 1, 0);

        camConfig->ImageAwb = get_value_or_default_uint((int)cam_sensor->status.awb, 1, 1);
        camConfig->ImageAec2 = get_value_or_default_uint((int)cam_sensor->status.aec2, 1, 1);
        camConfig->ImageAecValue = get_value_or_default_uint((int)cam_sensor->status.aec_value, 1200, 600);
        camConfig->ImageSpecialEffect = get_value_or_default_uint((int)cam_sensor->status.special_effect, 6, 0);
        camConfig->ImageWbMode = get_value_or_default_uint((int)cam_sensor->status.wb_mode, 4, 0);
        camConfig->ImageAeLevel = get_value_or_default_int((int)cam_sensor->status.ae_level, 5, -5, 0);

        camConfig->ImageDcw = get_value_or_default_uint((int)cam_sensor->status.dcw, 1, 1);
        camConfig->ImageBpc = get_value_or_default_uint((int)cam_sensor->status.bpc, 1, 1);
        camConfig->ImageWpc = get_value_or_default_uint((int)cam_sensor->status.wpc, 1, 1);
        camConfig->ImageAwbGain = get_value_or_default_uint((int)cam_sensor->status.awb_gain, 1, 1);
        camConfig->ImageAgcGain = clipInt((int)cam_sensor->status.agc_gain, 30, 8);

        camConfig->ImageRawGma = get_value_or_default_uint((int)cam_sensor->status.raw_gma, 1, 1);
        camConfig->ImageLenc = get_value_or_default_uint((int)cam_sensor->status.lenc, 1, 1);

        camConfig->ImageSharpness = get_value_or_default_int((int)cam_sensor->status.sharpness, 2, -2, 0);
        camConfig->ImageDenoiseLevel = get_value_or_default_uint((int)cam_sensor->status.denoise, 8, 0);

        return ESP_OK;
    }
    else
    {
        return ESP_FAIL;
    }
}

esp_err_t CCamera::set_camera_config_from_to(camera_controll_config_temp_t *camConfigFrom, camera_controll_config_temp_t *camConfigTo)
{
    camConfigTo->CamXclkFreqMhz = camConfigFrom->CamXclkFreqMhz;

    camConfigTo->ImageFrameSize = camConfigFrom->ImageFrameSize;

    camConfigTo->ImageContrast = camConfigFrom->ImageContrast;
    camConfigTo->ImageBrightness = camConfigFrom->ImageBrightness;
    camConfigTo->ImageSaturation = camConfigFrom->ImageSaturation;

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

    camConfigTo->ImageLedIntensity = camConfigFrom->ImageLedIntensity;

    camConfigTo->ImageZoomEnabled = camConfigFrom->ImageZoomEnabled;
    camConfigTo->ImageZoomOffsetX = camConfigFrom->ImageZoomOffsetX;
    camConfigTo->ImageZoomOffsetY = camConfigFrom->ImageZoomOffsetY;
    camConfigTo->ImageZoomSize = camConfigFrom->ImageZoomSize;

    camConfigTo->WaitBeforePicture = camConfigFrom->WaitBeforePicture;

    return ESP_OK;
}

esp_err_t CCamera::check_camera_settings_changed(void)
{
    set_camera_deep_sleep(false);

    // wenn die Kameraeinstellungen durch Erstellen eines neuen Referenzbildes verändert wurden, müssen sie neu gesetzt werden
    if (changedCameraSettings)
    {
        if (CamTempImage)
        {
            LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "check_camera_settings_changed(): CFstatus");
            set_sensor_controll_config(&CFstatus); // CFstatus >>> Kamera
            set_quality_zoom_size(&CFstatus);
            LedIntensity = CFstatus.ImageLedIntensity;
            CamTempImage = false;
        }
        else
        {
            LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "check_camera_settings_changed(): CCstatus");
            set_sensor_controll_config(&CCstatus); // CCstatus >>> Kamera
            set_quality_zoom_size(&CCstatus);
            LedIntensity = CCstatus.ImageLedIntensity;
            changedCameraSettings = false;
        }
    }

    return ESP_OK;
}

// only available on OV3660 and OV5640
// https://github.com/espressif/esp32-camera/issues/672
esp_err_t CCamera::set_camera_deep_sleep(bool status)
{
    if (CamDeepSleepEnable != status)
    {
        CamDeepSleepEnable = status;

        // sensor_t *cam_sensor = esp_camera_sensor_get();
        if (cam_sensor != NULL)
        {
            std::string temp_status = "unsupported";
            if (CamSensorId == OV2640_PID)
            {
                // OV2640 Standby mode
                uint8_t reg = cam_sensor->get_reg(cam_sensor, 0x09, 0xFF);
                cam_sensor->set_reg(cam_sensor, 0x09, 0xFF, status ? (reg |= 0x10) : (reg &= ~0x10));
                temp_status = status ? "enabled" : "disabled";
            }
            else if ((CamSensorId == OV3660_PID) || (CamSensorId == OV5640_PID))
            {
                // OV3660/OV5640 DeepSleep mode
                cam_sensor->set_reg(cam_sensor, 0x3008, 0x42, status ? 0x42 : 0x02);
                temp_status = status ? "enabled" : "disabled";
            }

            LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "DeepSleep: " + temp_status);
            vTaskDelay(200 / portTICK_PERIOD_MS);
        }
        else
        {
            LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "set_camera_deep_sleep(), Failed to get Cam control structure");
            return ESP_FAIL;
        }
    }

    return ESP_OK;
}

// on the OV5640, gainceiling must be set with the real value (x2>>>gainceilingLevel = 2, .... x128>>>gainceilingLevel = 128)
esp_err_t CCamera::set_camera_gainceiling(gainceiling_t gainceilingLevel)
{
    // sensor_t *cam_sensor = esp_camera_sensor_get();
    if (cam_sensor != NULL)
    {
        if (CamSensorId == OV2640_PID)
        {
            cam_sensor->set_gainceiling(cam_sensor, gainceilingLevel); // Image gain (GAINCEILING_x2, x4, x8, x16, x32, x64 or x128)
        }
        else
        {
            int _level = (1 << ((int)gainceilingLevel + 1));
            int ret = cam_sensor->set_reg(cam_sensor, 0x3A18, 0xFF, (_level >> 8) & 3) || cam_sensor->set_reg(cam_sensor, 0x3A19, 0xFF, _level & 0xFF);

            if (ret == 0)
            {
                // ESP_LOGD(TAG, "Set gainceiling to: %d", gainceilingLevel);
                cam_sensor->status.gainceiling = gainceilingLevel;
            }
        }
    }
    else
    {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "set_camera_gainceiling(), Failed to get Cam control structure");
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t CCamera::set_camera_sharpness(bool autoSharpnessEnabled, int sharpnessLevel)
{
    // sensor_t *cam_sensor = esp_camera_sensor_get();
    if (cam_sensor != NULL)
    {
        if (CamSensorId == OV2640_PID)
        {
            sharpnessLevel = min(2, max(-2, sharpnessLevel));
            // The OV2640 does not officially support sharpness, so the detour is made with the ov2640_sharpness.cpp.
            if (autoSharpnessEnabled)
            {
                ov2640_enable_auto_sharpness(cam_sensor);
            }
            else
            {
                ov2640_set_sharpness(cam_sensor, sharpnessLevel);
            }
        }
        else
        {
            sharpnessLevel = min(3, max(-3, sharpnessLevel));
            // for CAMERA_OV5640 and CAMERA_OV3660
            if (autoSharpnessEnabled)
            {
                // autoSharpness is not supported, default to zero
                cam_sensor->set_sharpness(cam_sensor, 0);
            }
            else
            {
                cam_sensor->set_sharpness(cam_sensor, sharpnessLevel);
            }
        }
    }
    else
    {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "SetCamSharpness(), Failed to get Cam control structure");
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t CCamera::set_camera_special_effect(int specialEffect)
{
    // sensor_t *cam_sensor = esp_camera_sensor_get();
    if (cam_sensor != NULL)
    {
        if (CamSensorId == OV2640_PID)
        {
            ov2640_set_special_effect(cam_sensor, specialEffect);
        }
        else
        {
            cam_sensor->set_special_effect(cam_sensor, specialEffect);
        }
    }
    else
    {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "set_camera_special_effect(), Failed to get Cam control structure");
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t CCamera::set_camera_contrast_brightness(int _contrast, int _brightness)
{
    // sensor_t *cam_sensor = esp_camera_sensor_get();
    if (cam_sensor != NULL)
    {
        if (CamSensorId == OV2640_PID)
        {
            ov2640_set_contrast_brightness(cam_sensor, _contrast, _brightness);
        }
        else
        {
            cam_sensor->set_contrast(cam_sensor, _contrast);     // -2 to 2
            cam_sensor->set_brightness(cam_sensor, _brightness); // -2 to 2
        }
    }
    else
    {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "set_camera_contrast_brightness(), Failed to get Cam control structure");
        return ESP_FAIL;
    }

    return ESP_OK;
}

// - It always zooms to the image center when offsets are zero
// - if imageSize = 0 then the image is not zoomed
// - if imageSize = max value, then the image is fully zoomed in
// - a zoom step is >>> Width + 32 px / Height + 24 px
void CCamera::sanitize_zoom_params(camera_controll_config_temp_t *camConfig, int imageSize, int frameSizeX, int frameSizeY, int &imageWidth, int &imageHeight, int &zoomOffsetX, int &zoomOffsetY)
{
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
}

esp_err_t CCamera::set_zoom_size(camera_controll_config_temp_t *camConfig)
{
    // sensor_t *cam_sensor = esp_camera_sensor_get();
    if (cam_sensor != NULL)
    {
        if (camConfig->ImageZoomEnabled)
        {
            int _imageSize_temp = 0;

            int _imageWidth = camConfig->ImageWidth;
            int _imageHeight = camConfig->ImageHeight;

            int _offsetx = camConfig->ImageZoomOffsetX;
            int _offsety = camConfig->ImageZoomOffsetY;

            int frameSizeX = 1600;
            int frameSizeY = 1200;

            switch (CamSensorId)
            {
            case OV5640_PID:
                frameSizeX = 2592;
                frameSizeY = 1944;
                // max imageSize = ((frameSizeX - camConfig->ImageWidth) / 8 / 4) - 1
                // 59 = ((2560 - 640) / 8 / 4) - 1
                if (camConfig->ImageZoomSize < 59)
                {
                    _imageSize_temp = (59 - camConfig->ImageZoomSize);
                }
                break;

            case OV3660_PID:
                frameSizeX = 2048;
                frameSizeY = 1536;
                // max imageSize = ((frameSizeX - camConfig->ImageWidth) / 8 / 4) -1
                // 43 = ((2048 - 640) / 8 / 4) - 1
                if (camConfig->ImageZoomSize < 43)
                {
                    _imageSize_temp = (43 - camConfig->ImageZoomSize);
                }
                break;

            case OV2640_PID:
                frameSizeX = 1600;
                frameSizeY = 1200;
                // max imageSize = ((frameSizeX - camConfig->ImageWidth) / 8 / 4) -1
                // 29 = ((1600 - 640) / 8 / 4) - 1
                if (camConfig->ImageZoomSize < 29)
                {
                    _imageSize_temp = (29 - camConfig->ImageZoomSize);
                }
                break;

            default:
                // do nothing
                break;
            }

            sanitize_zoom_params(camConfig, _imageSize_temp, frameSizeX, frameSizeY, _imageWidth, _imageHeight, _offsetx, _offsety);
            set_camera_window(camConfig, frameSizeX, frameSizeY, _offsetx, _offsety, _imageWidth, _imageHeight);
        }
        else
        {
            cam_sensor->set_framesize(cam_sensor, camConfig->ImageFrameSize);
        }
    }
    else
    {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "set_zoom_size(), Failed to get Cam control structure");
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t CCamera::set_quality_zoom_size(camera_controll_config_temp_t *camConfig)
{
    // sensor_t *cam_sensor = esp_camera_sensor_get();
    if (cam_sensor != NULL)
    {
        camConfig->ImageQuality = min(63, max(8, camConfig->ImageQuality));

        set_image_width_height_from_resolution(camConfig->ImageFrameSize);

        cam_sensor->set_quality(cam_sensor, camConfig->ImageQuality);
        set_zoom_size(camConfig);
    }
    else
    {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "set_quality_zoom_size(), Failed to get Cam control structure");
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t CCamera::set_camera_window(camera_controll_config_temp_t *camConfig, int frameSizeX, int frameSizeY, int xOffset, int yOffset, int xTotal, int yTotal)
{
    // sensor_t *cam_sensor = esp_camera_sensor_get();
    if (cam_sensor != NULL)
    {
        if (CamSensorId == OV2640_PID)
        {
            cam_sensor->set_res_raw(cam_sensor, 0, 0, 0, 0, xOffset, yOffset, xTotal, yTotal, camConfig->ImageWidth, camConfig->ImageHeight, false, false);
        }
        else
        {
            // for CAMERA_OV5640 and CAMERA_OV3660
            bool scale = !(camConfig->ImageWidth == xTotal && camConfig->ImageHeight == yTotal);
            bool binning = (xTotal >= (frameSizeX >> 1));

            if (camConfig->ImageVflip == 1)
            {
                cam_sensor->set_res_raw(cam_sensor, xOffset, yOffset, xOffset + xTotal - 1, yOffset + yTotal - 1, 0, 0, frameSizeX, frameSizeY, camConfig->ImageWidth, camConfig->ImageHeight, scale, binning);
            }
            else
            {
                cam_sensor->set_res_raw(cam_sensor, xOffset, yOffset, xOffset + xTotal, yOffset + yTotal, 0, 0, frameSizeX, frameSizeY, camConfig->ImageWidth, camConfig->ImageHeight, scale, binning);
            }
        }
    }
    else
    {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "set_camera_window(), Failed to get Cam control structure");
        return ESP_FAIL;
    }

    return ESP_OK;
}

static size_t jpg_encode_stream(void *arg, size_t index, const void *data, size_t len)
{
    jpg_chunking_t *j = (jpg_chunking_t *)arg;

    if (!index)
    {
        j->len = 0;
    }

    if (httpd_resp_send_chunk(j->req, (const char *)data, len) != ESP_OK)
    {
        return 0;
    }

    j->len += len;

    return len;
}

esp_err_t CCamera::CaptureToBasisImage(CImageBasis *_Image, int flash_duration)
{
    _Image->EmptyImage(); // Delete previous stored raw image -> black image

    check_camera_settings_changed();
    LEDOnOff(true); // Status-LED on

    if (flash_duration > 0)
    {
        CaptureToBasisImageLed = true;
        LightOnOff(true); // Flash-LED on
        const TickType_t xDelay = flash_duration / portTICK_PERIOD_MS;
        vTaskDelay(xDelay);
    }

    // esp_camera_return_all();
    camera_fb_t *fb = esp_camera_fb_get();
    esp_camera_fb_return(fb);
    fb = esp_camera_fb_get();

    if (flash_duration > 0)
    {
        CaptureToBasisImageLed = false;
        if (!CaptureToFileLed && !CaptureToHTTPLed && !CaptureToStreamLed)
        {
            if (flash_duration > 0)
            {
                LightOnOff(false); // Flash-LED off
            }
        }
    }

    if (fb == NULL)
    {
        if (!CaptureToFileLed && !CaptureToHTTPLed && !CaptureToStreamLed)
        {
            LEDOnOff(false); // Status-LED off
            set_camera_deep_sleep(true);
        }

        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "capture_to_basis_image(): Capture Failed. Check camera module and/or proper electrical connection");

        return ESP_FAIL;
    }

    if (DemoMode)
    {
        // Use images stored on SD-Card instead of camera image
        /* Replace Framebuffer with image from SD-Card */
        loadNextDemoImage(fb);
    }

    CImageBasis *_temp_Image = new CImageBasis("tempImage");

    if (_temp_Image)
    {
        _temp_Image->LoadFromMemory(fb->buf, fb->len);
    }
    else
    {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "CaptureToBasisImage: Can't allocate _temp_Image");
    }

    // esp_camera_return_all();
    esp_camera_fb_return(fb);

    if (!CaptureToFileLed && !CaptureToHTTPLed && !CaptureToStreamLed)
    {
        LEDOnOff(false); // Status-LED off
        set_camera_deep_sleep(true);
    }

    if (_temp_Image == NULL)
    {
        return ESP_FAIL;
    }

    stbi_uc *p_target;
    stbi_uc *p_source;
    int channels = 3;

    int width = CCstatus.ImageWidth;
    int height = CCstatus.ImageHeight;
    if (CamTempImage)
    {
        width = CFstatus.ImageWidth;
        height = CFstatus.ImageHeight;
    }

    for (int x = 0; x < width; ++x)
    {
        for (int y = 0; y < height; ++y)
        {
            p_target = _Image->rgb_image + (channels * (y * width + x));
            p_source = _temp_Image->rgb_image + (channels * (y * width + x));

            for (int c = 0; c < channels; c++)
            {
                p_target[c] = p_source[c];
            }
        }
    }

    delete _temp_Image;

    return ESP_OK;
}

esp_err_t CCamera::CaptureToFile(std::string nm, int flash_duration)
{
    check_camera_settings_changed();
    LEDOnOff(true); // Status-LED on

    if (flash_duration > 0)
    {
        CaptureToFileLed = true;
        LightOnOff(true); // Flash-LED on
        const TickType_t xDelay = flash_duration / portTICK_PERIOD_MS;
        vTaskDelay(xDelay);
    }

    // esp_camera_return_all();
    camera_fb_t *fb = esp_camera_fb_get();
    esp_camera_fb_return(fb);
    fb = esp_camera_fb_get();

    if (flash_duration > 0)
    {
        CaptureToFileLed = false;
        if (!CaptureToBasisImageLed && !CaptureToHTTPLed && !CaptureToStreamLed)
        {
            if (flash_duration > 0)
            {
                LightOnOff(false); // Flash-LED off
            }
        }
    }

    if (fb == NULL)
    {
        if (!CaptureToBasisImageLed && !CaptureToHTTPLed && !CaptureToStreamLed)
        {
            LEDOnOff(false); // Status-LED off
            set_camera_deep_sleep(true);
        }

        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "capture_to_file(): Capture Failed. Check camera module and/or proper electrical connection");

        return ESP_FAIL;
    }

    nm = FormatFileName(nm);
    std::string ftype = toUpper(getFileType(nm));

    uint8_t *buf = NULL;
    size_t buf_len = 0;
    bool converted = false;

    if (ftype.compare("BMP") == 0)
    {
        frame2bmp(fb, &buf, &buf_len);
        converted = true;
    }

    if (ftype.compare("JPG") == 0)
    {
        if (fb->format != PIXFORMAT_JPEG)
        {
            int _ImageQuality = CCstatus.ImageQuality;
            if (CamTempImage)
            {
                _ImageQuality = CFstatus.ImageQuality;
            }

            bool jpeg_converted = frame2jpg(fb, (100 - _ImageQuality), &buf, &buf_len);
            converted = true;

            if (!jpeg_converted)
            {
                ESP_LOGE(TAG, "JPEG compression failed");
            }
        }
        else
        {
            buf_len = fb->len;
            buf = fb->buf;
        }
    }

    // esp_camera_return_all();
    esp_camera_fb_return(fb);

    if (!CaptureToBasisImageLed && !CaptureToHTTPLed && !CaptureToStreamLed)
    {
        LEDOnOff(false); // Status-LED off
        set_camera_deep_sleep(true);
    }

    FILE *fp = fopen(nm.c_str(), "wb");
    if (fp == NULL)
    {
        // If an error occurs during the file creation
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "CaptureToFile: Failed to open file " + nm);
    }
    else
    {
        fwrite(buf, sizeof(uint8_t), buf_len, fp);
        fclose(fp);
    }

    if (converted)
    {
        free(buf);
    }

    return ESP_OK;
}

esp_err_t CCamera::CaptureToHTTP(httpd_req_t *req, int flash_duration)
{
    esp_err_t res = ESP_OK;
    size_t fb_len = 0;
    int64_t fr_start = esp_timer_get_time();

    check_camera_settings_changed();
    LEDOnOff(true); // Status-LED on

    if (flash_duration > 0)
    {
        CaptureToHTTPLed = true;
        LightOnOff(true); // Flash-LED on
        const TickType_t xDelay = flash_duration / portTICK_PERIOD_MS;
        vTaskDelay(xDelay);
    }

    // esp_camera_return_all();
    camera_fb_t *fb = esp_camera_fb_get();
    esp_camera_fb_return(fb);
    fb = esp_camera_fb_get();

    if (flash_duration > 0)
    {
        CaptureToHTTPLed = false;
        if (!CaptureToBasisImageLed && !CaptureToFileLed && !CaptureToStreamLed)
        {
            if (flash_duration > 0)
            {
                LightOnOff(false); // Flash-LED off
            }
        }
    }

    if (fb == NULL)
    {
        if (!CaptureToBasisImageLed && !CaptureToFileLed && !CaptureToStreamLed)
        {
            LEDOnOff(false); // Status-LED off
            set_camera_deep_sleep(true);
        }

        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "capture_to_http(): Capture Failed. Check camera module and/or proper electrical connection");
        httpd_resp_send_500(req);

        return ESP_FAIL;
    }

    res = httpd_resp_set_type(req, "image/jpeg");

    if (res == ESP_OK)
    {
        res = httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=raw.jpg");
    }

    if (res == ESP_OK)
    {
        if (DemoMode)
        {
            // Use images stored on SD-Card instead of camera image
            LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "Using Demo image!");
            /* Replace Framebuffer with image from SD-Card */
            loadNextDemoImage(fb);

            res = httpd_resp_send(req, (const char *)fb->buf, fb->len);
        }
        else
        {
            if (fb->format == PIXFORMAT_JPEG)
            {
                fb_len = fb->len;
                res = httpd_resp_send(req, (const char *)fb->buf, fb->len);
            }
            else
            {
                int _ImageQuality = CCstatus.ImageQuality;
                if (CamTempImage)
                {
                    _ImageQuality = CFstatus.ImageQuality;
                }

                jpg_chunking_t jchunk = {req, 0};
                frame2jpg_cb(fb, (100 - _ImageQuality), jpg_encode_stream, &jchunk);
                httpd_resp_send_chunk(req, NULL, 0);
                fb_len = jchunk.len;
            }
        }
    }

    // esp_camera_return_all();
    esp_camera_fb_return(fb);

    if (!CaptureToBasisImageLed && !CaptureToFileLed && !CaptureToStreamLed)
    {
        LEDOnOff(false); // Status-LED off
        set_camera_deep_sleep(true);
    }

    int64_t fr_end = esp_timer_get_time();
    ESP_LOGI(TAG, "CaptureToHTTP() - JPG: %dKB %dms", (int)(fb_len / 1024), (int)((fr_end - fr_start) / 1000));

    return res;
}

esp_err_t CCamera::CaptureToStream(httpd_req_t *req, bool FlashlightOn)
{
    LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Live stream started");

    esp_err_t res = ESP_OK;
    size_t fb_len = 0;
    int64_t fr_start;
    char *part_buf[64];

    check_camera_settings_changed();
    if (FlashlightOn)
    {
        LEDOnOff(true);   // Status-LED on
        LightOnOff(true); // Flash-LED on
    }

    // esp_camera_return_all();
    camera_fb_t *fb = esp_camera_fb_get();
    esp_camera_fb_return(fb);
    fb = esp_camera_fb_get();

    if (fb == NULL)
    {
        CaptureToStreamLed = false;
        if (!CaptureToBasisImageLed && !CaptureToFileLed && !CaptureToHTTPLed)
        {
            LEDOnOff(false); // Status-LED off
            set_camera_deep_sleep(true);
            if (FlashlightOn)
            {
                LightOnOff(false); // Flash-LED off
            }
        }

        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "capture_to_stream(): Capture Failed. Check camera module and/or proper electrical connection");
        httpd_resp_send_500(req);

        return ESP_FAIL;
    }

    httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
    httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));

    esp_camera_fb_return(fb);

    while (1)
    {
        fr_start = esp_timer_get_time();

        fb = esp_camera_fb_get();
        if (!fb)
        {
            LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "CaptureToStream: Camera framebuffer not available");
            break;
        }

        fb_len = fb->len;

        size_t hlen = snprintf((char *)part_buf, sizeof(part_buf), _STREAM_PART, fb_len);
        res = httpd_resp_send_chunk(req, (const char *)part_buf, hlen);

        if (res == ESP_OK)
        {
            res = httpd_resp_send_chunk(req, (const char *)fb->buf, fb_len);
        }

        if (res == ESP_OK)
        {
            res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
        }

        esp_camera_fb_return(fb);

        int64_t fr_end = esp_timer_get_time();
        ESP_LOGD(TAG, "CaptureToStream() - JPG: %dKB %dms", (int)(fb_len / 1024), (int)((fr_end - fr_start) / 1000));

        if (res != ESP_OK)
        {
            // Exit loop, e.g. also when closing the webpage
            break;
        }

        int64_t fr_delta_ms = (fr_end - fr_start) / 1000;

        if (CAM_LIVESTREAM_REFRESHRATE > fr_delta_ms)
        {
            const TickType_t xDelay = (CAM_LIVESTREAM_REFRESHRATE - fr_delta_ms) / portTICK_PERIOD_MS;
            ESP_LOGD(TAG, "Stream: sleep for: %ldms", (long)xDelay * 10);
            vTaskDelay(xDelay);
        }
    }

    // esp_camera_return_all();
    esp_camera_fb_return(fb);

    CaptureToStreamLed = false;
    if (!CaptureToBasisImageLed && !CaptureToFileLed && !CaptureToHTTPLed)
    {
        LEDOnOff(false); // Status-LED off
        set_camera_deep_sleep(true);
        if (FlashlightOn)
        {
            LightOnOff(false); // Flash-LED off
        }
    }

    LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Live stream stopped");

    return res;
}

void CCamera::LightOnOff(bool status)
{
    ConfigGpioHandler *gpioHandler = gpio_handler_get();

    if ((gpioHandler != NULL) && (gpioHandler->isEnabled()))
    {
        ESP_LOGD(TAG, "Use gpioHandler to trigger flashlight");
        gpioHandler->flashLightEnable(status);
    }
    else
    {
#ifdef USE_PWM_LEDFLASH
        if (status)
        {
            ESP_LOGD(TAG, "Internal Flash-LED turn on with PWM %d", Camera.LedIntensity);
            ESP_ERROR_CHECK(ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, Camera.LedIntensity));
            // Update duty to apply the new value
            ESP_ERROR_CHECK(ledc_update_duty(LEDC_MODE, LEDC_CHANNEL));
        }
        else
        {
            ESP_LOGD(TAG, "Internal Flash-LED turn off PWM");
            ESP_ERROR_CHECK(ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, 0));
            ESP_ERROR_CHECK(ledc_update_duty(LEDC_MODE, LEDC_CHANNEL));
        }
#else
        // Init the GPIO
        gpio_pad_select_gpio(FLASH_GPIO);

        // Set the GPIO as a push/pull output
        gpio_set_direction(FLASH_GPIO, GPIO_MODE_OUTPUT);

        if (status)
        {
            gpio_set_level(FLASH_GPIO, 1);
        }
        else
        {
            gpio_set_level(FLASH_GPIO, 0);
        }
#endif
    }
}

void CCamera::LEDOnOff(bool status)
{
    if (xHandle_task_StatusLED == NULL)
    {
        // Init the GPIO
        gpio_pad_select_gpio(BLINK_GPIO);

        /* Set the GPIO as a push/pull output */
        gpio_set_direction(BLINK_GPIO, GPIO_MODE_OUTPUT);

        if (!status)
        {
            gpio_set_level(BLINK_GPIO, 1);
        }
        else
        {
            gpio_set_level(BLINK_GPIO, 0);
        }
    }
}

void CCamera::set_image_width_height_from_resolution(framesize_t resol)
{
    if (resol == FRAMESIZE_QVGA)
    {
        CCstatus.ImageWidth = 320;
        CCstatus.ImageHeight = 240;
    }
    else if (resol == FRAMESIZE_VGA)
    {
        CCstatus.ImageWidth = 640;
        CCstatus.ImageHeight = 480;
    }
    else if (resol == FRAMESIZE_SVGA)
    {
        CCstatus.ImageWidth = 800;
        CCstatus.ImageHeight = 600;
    }
    else if (resol == FRAMESIZE_XGA)
    {
        CCstatus.ImageWidth = 1024;
        CCstatus.ImageHeight = 768;
    }
    else if (resol == FRAMESIZE_HD)
    {
        CCstatus.ImageWidth = 1280;
        CCstatus.ImageHeight = 720;
    }
    else if (resol == FRAMESIZE_SXGA)
    {
        CCstatus.ImageWidth = 1280;
        CCstatus.ImageHeight = 1024;
    }
    else if (resol == FRAMESIZE_UXGA)
    {
        CCstatus.ImageWidth = 1600;
        CCstatus.ImageHeight = 1200;
    }
    else if (resol == FRAMESIZE_QXGA)
    {
        CCstatus.ImageWidth = 2048;
        CCstatus.ImageHeight = 1536;
    }
    else if (resol == FRAMESIZE_WQXGA)
    {
        CCstatus.ImageWidth = 2560;
        CCstatus.ImageHeight = 1600;
    }
    else if (resol == FRAMESIZE_QSXGA)
    {
        CCstatus.ImageWidth = 2560;
        CCstatus.ImageHeight = 1920;
    }
    else
    {
        CCstatus.ImageWidth = 640;
        CCstatus.ImageHeight = 480;
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

    // return CCstatus.ImageFrameSize;
}

std::vector<std::string> demoFiles;

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

    demoImage = (uint8_t *)malloc(DEMO_IMAGE_SIZE);

    if (demoImage == NULL)
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

    DemoMode = true;
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

    if (fileSize > DEMO_IMAGE_SIZE)
    {
        char buf[100];
        snprintf(buf, sizeof(buf), "Demo Image (%d bytes) is larger than provided buffer (%d bytes)!", (int)fileSize, DEMO_IMAGE_SIZE);
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, std::string(buf));
        return false;
    }

    readBytes = fread(demoImage, 1, DEMO_IMAGE_SIZE, fp);
    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "read " + std::to_string(readBytes) + " bytes");
    fclose(fp);

    fb->buf = demoImage; // Update pointer
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
