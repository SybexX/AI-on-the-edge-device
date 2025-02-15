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
#include "server_GPIO.h"

#include "../../include/defines.h"

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

static const char *TAG = "CAM";

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
    .ledc_timer = LEDC_TIMER_0,     // LEDC timer to be used for generating XCLK
    .ledc_channel = LEDC_CHANNEL_0, // LEDC channel to be used for generating XCLK

    .pixel_format = PIXFORMAT_JPEG, // YUV422,GRAYSCALE,RGB565,JPEG
    .frame_size = FRAMESIZE_VGA,    // QQVGA-UXGA Do not use sizes above QVGA when not JPEG
    // .frame_size = FRAMESIZE_UXGA,    //QQVGA-UXGA Do not use sizes above QVGA when not JPEG
    .jpeg_quality = 12,                 // 0-63 lower number means higher quality
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
#ifdef DEBUG_DETAIL_ON
    ESP_LOGD(TAG, "CreateClassCamera");
#endif
    CCstatus.WaitBeforePicture = 2;

    ledc_init();
}

esp_err_t CCamera::InitCam(void)
{
    ESP_LOGD(TAG, "Init Camera");

    TickType_t cam_xDelay = 100 / portTICK_PERIOD_MS;

    // De-init in case it was already initialized
    esp_camera_deinit();
    vTaskDelay(cam_xDelay);

    // CCstatus.CamXclkFreqMhz = (camera_config.xclk_freq_hz / 1000000);

    // initialize the camera
    esp_err_t err = esp_camera_init(&camera_config);
    vTaskDelay(cam_xDelay);

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
        Camera.CamSensor_id = sensor->id.PID;

        // Dump camera module, warn for unsupported modules.
        switch (Camera.CamSensor_id)
        {
        case OV2640_PID:
            ESP_LOGI(TAG, "OV2640 camera module detected");
            Camera.CamInitSuccessful = true;
            break;
        case OV3660_PID:
            ESP_LOGI(TAG, "OV3660 camera module detected");
            Camera.CamInitSuccessful = true;
            break;
        case OV5640_PID:
            ESP_LOGI(TAG, "OV5640 camera module detected");
            Camera.CamInitSuccessful = true;
            break;
        default:
            ESP_LOGE(TAG, "Camera module is unknown and not properly supported!");
        }
    }

    if (Camera.CamInitSuccessful)
    {
        return ESP_OK;
    }
    else
    {
        return ESP_FAIL;
    }
}

bool CCamera::testCamera(void)
{
    bool success;
    camera_fb_t *fb = esp_camera_fb_get();

    if (fb)
    {
        success = true;
    }
    else
    {
        success = false;
    }

    esp_camera_fb_return(fb);

    return success;
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
    Camera.LedIntensity = (int)((float)(std::min(std::max(0, _intrel), 100)) / 100 * 8191);
    ESP_LOGD(TAG, "Set led_intensity to %i of 8191", Camera.LedIntensity);
    return Camera.LedIntensity;
}

bool CCamera::getCameraInitSuccessful(void)
{
    return Camera.CamInitSuccessful;
}

esp_err_t CCamera::setSensorDatenFromCCstatus(void)
{
    sensor_t *sensor = esp_camera_sensor_get();

    if (sensor != NULL)
    {
        sensor->set_xclk(sensor, camera_config.ledc_timer, CCstatus.CamXclkFreqMhz);

        sensor->set_framesize(sensor, CCstatus.ImageFrameSize);
		
        // sensor->set_contrast(sensor, CCstatus.ImageContrast);     // -2 to 2
        // sensor->set_brightness(sensor, CCstatus.ImageBrightness); // -2 to 2
        SetCamContrastBrightness(sensor, CCstatus.ImageContrast, CCstatus.ImageBrightness);
		
        sensor->set_saturation(sensor, CCstatus.ImageSaturation); // -2 to 2

        sensor->set_quality(sensor, CCstatus.ImageQuality); // 0 - 63
		
        // sensor->set_gainceiling(sensor, CCstatus.ImageGainceiling); // Image gain (GAINCEILING_x2, x4, x8, x16, x32, x64 or x128)
        SetCamGainceiling(sensor, CCstatus.ImageGainceiling);
		
        sensor->set_gain_ctrl(sensor, CCstatus.ImageAgc);     // 0 = disable , 1 = enable
        sensor->set_exposure_ctrl(sensor, CCstatus.ImageAec); // 0 = disable , 1 = enable
        sensor->set_hmirror(sensor, CCstatus.ImageHmirror); // 0 = disable , 1 = enable
        sensor->set_vflip(sensor, CCstatus.ImageVflip);     // 0 = disable , 1 = enable
		
        sensor->set_whitebal(sensor, CCstatus.ImageAwb);     // 0 = disable , 1 = enable
        sensor->set_aec2(sensor, CCstatus.ImageAec2);       // 0 = disable , 1 = enable
        sensor->set_aec_value(sensor, CCstatus.ImageAecValue); // 0 to 1200
        // sensor->set_special_effect(sensor, CCstatus.ImageSpecialEffect); // 0 to 6 (0 - No Effect, 1 - Negative, 2 - Grayscale, 3 - Red Tint, 4 - Green Tint, 5 - Blue Tint, 6 - Sepia)
        SetCamSpecialEffect(sensor, CCstatus.ImageSpecialEffect);
        sensor->set_wb_mode(sensor, CCstatus.ImageWbMode);               // 0 to 4 - if awb_gain enabled (0 - Auto, 1 - Sunny, 2 - Cloudy, 3 - Office, 4 - Home)
        sensor->set_ae_level(sensor, CCstatus.ImageAeLevel);   // -2 to 2
		
        sensor->set_dcw(sensor, CCstatus.ImageDcw); // 0 = disable , 1 = enable
        sensor->set_bpc(sensor, CCstatus.ImageBpc); // 0 = disable , 1 = enable
        sensor->set_wpc(sensor, CCstatus.ImageWpc); // 0 = disable , 1 = enable
        sensor->set_awb_gain(sensor, CCstatus.ImageAwbGain); // 0 = disable , 1 = enable
        sensor->set_agc_gain(sensor, CCstatus.ImageAgcGain);   // 0 to 30
		
        sensor->set_raw_gma(sensor, CCstatus.ImageRawGma); // 0 = disable , 1 = enable
        sensor->set_lenc(sensor, CCstatus.ImageLenc);         // 0 = disable , 1 = enable

        // sensor->set_sharpness(sensor, CCstatus.ImageSharpness);   // auto-sharpness is not officially supported, default to 0
        SetCamSharpness(sensor, CCstatus.ImageAutoSharpness, CCstatus.ImageSharpness);
        sensor->set_denoise(sensor, CCstatus.ImageDenoiseLevel); // The OV2640 does not support it, OV3660 and OV5640 (0 to 8)

        TickType_t cam_xDelay = 100 / portTICK_PERIOD_MS;
        vTaskDelay(cam_xDelay);

        return ESP_OK;
    }
    else
    {
        return ESP_FAIL;
    }
}

esp_err_t CCamera::setSensorDatenFromCFstatus(void)
{
    sensor_t *sensor = esp_camera_sensor_get();

    if (sensor != NULL)
    {
        sensor->set_xclk(sensor, camera_config.ledc_timer, CFstatus.CamXclkFreqMhz);

        sensor->set_framesize(sensor, CFstatus.ImageFrameSize);

        // sensor->set_contrast(sensor, CFstatus.ImageContrast);     // -2 to 2
        // sensor->set_brightness(sensor, CFstatus.ImageBrightness); // -2 to 2
        Camera.SetCamContrastBrightness(sensor, CFstatus.ImageContrast, CFstatus.ImageBrightness);
		
        sensor->set_saturation(sensor, CFstatus.ImageSaturation); // -2 to 2

        sensor->set_quality(sensor, CFstatus.ImageQuality); // 0 - 63

        // sensor->set_gainceiling(sensor, CFstatus.ImageGainceiling); // Image gain (GAINCEILING_x2, x4, x8, x16, x32, x64 or x128)
        Camera.SetCamGainceiling(sensor, CFstatus.ImageGainceiling);

        sensor->set_gain_ctrl(sensor, CFstatus.ImageAgc);     // 0 = disable , 1 = enable
        sensor->set_exposure_ctrl(sensor, CFstatus.ImageAec); // 0 = disable , 1 = enable
        sensor->set_hmirror(sensor, CFstatus.ImageHmirror);   // 0 = disable , 1 = enable
        sensor->set_vflip(sensor, CFstatus.ImageVflip);       // 0 = disable , 1 = enable

        sensor->set_whitebal(sensor, CFstatus.ImageAwb);       // 0 = disable , 1 = enable
        sensor->set_aec2(sensor, CFstatus.ImageAec2);          // 0 = disable , 1 = enable
        sensor->set_aec_value(sensor, CFstatus.ImageAecValue); // 0 to 1200
        // sensor->set_special_effect(sensor, CFstatus.ImageSpecialEffect); // 0 to 6 (0 - No Effect, 1 - Negative, 2 - Grayscale, 3 - Red Tint, 4 - Green Tint, 5 - Blue Tint, 6 - Sepia)
        Camera.SetCamSpecialEffect(sensor, CFstatus.ImageSpecialEffect);
        sensor->set_wb_mode(sensor, CFstatus.ImageWbMode);   // 0 to 4 - if awb_gain enabled (0 - Auto, 1 - Sunny, 2 - Cloudy, 3 - Office, 4 - Home)
        sensor->set_ae_level(sensor, CFstatus.ImageAeLevel); // -2 to 2

        sensor->set_dcw(sensor, CFstatus.ImageDcw);          // 0 = disable , 1 = enable
        sensor->set_bpc(sensor, CFstatus.ImageBpc);          // 0 = disable , 1 = enable
        sensor->set_wpc(sensor, CFstatus.ImageWpc);          // 0 = disable , 1 = enable
        sensor->set_awb_gain(sensor, CFstatus.ImageAwbGain); // 0 = disable , 1 = enable
        sensor->set_agc_gain(sensor, CFstatus.ImageAgcGain); // 0 to 30

        sensor->set_raw_gma(sensor, CFstatus.ImageRawGma); // 0 = disable , 1 = enable
        sensor->set_lenc(sensor, CFstatus.ImageLenc);      // 0 = disable , 1 = enable

        // sensor->set_sharpness(sensor, CFstatus.ImageSharpness);   // auto-sharpness is not officially supported, default to 0
        Camera.SetCamSharpness(sensor, CFstatus.ImageAutoSharpness, CFstatus.ImageSharpness);
        sensor->set_denoise(sensor, CFstatus.ImageDenoiseLevel); // The OV2640 does not support it, OV3660 and OV5640 (0 to 8)

        TickType_t xDelay2 = 100 / portTICK_PERIOD_MS;
        vTaskDelay(xDelay2);

        return ESP_OK;
    }
    else
    {
        return ESP_FAIL;
    }
}

esp_err_t CCamera::getSensorDatenToCCstatus(void)
{
    sensor_t *sensor = esp_camera_sensor_get();

    if (sensor != NULL)
    {
        Camera.CamSensor_id = sensor->id.PID;
        CCstatus.CamXclkFreqMhz = (sensor->xclk_freq_hz / 1000000);

        CCstatus.ImageFrameSize = (framesize_t)sensor->status.framesize;
		
        CCstatus.ImageContrast = sensor->status.contrast;
        CCstatus.ImageBrightness = sensor->status.brightness;
        CCstatus.ImageSaturation = sensor->status.saturation;
		
        CCstatus.ImageQuality = sensor->status.quality;
		
        CCstatus.ImageGainceiling = (gainceiling_t)sensor->status.gainceiling;

        CCstatus.ImageAgc = sensor->status.agc;
        CCstatus.ImageAec = sensor->status.aec;
        CCstatus.ImageHmirror = sensor->status.hmirror;
        CCstatus.ImageVflip = sensor->status.vflip;
		
        CCstatus.ImageAwb = sensor->status.awb;
        CCstatus.ImageAec2 = sensor->status.aec2;
        CCstatus.ImageAecValue = sensor->status.aec_value;
        CCstatus.ImageSpecialEffect = sensor->status.special_effect;
        CCstatus.ImageWbMode = sensor->status.wb_mode;
        CCstatus.ImageAeLevel = sensor->status.ae_level;
		
        CCstatus.ImageDcw = sensor->status.dcw;
        CCstatus.ImageBpc = sensor->status.bpc;
        CCstatus.ImageWpc = sensor->status.wpc;
        CCstatus.ImageAwbGain = sensor->status.awb_gain;
        CCstatus.ImageAgcGain = sensor->status.agc_gain;
		
        CCstatus.ImageRawGma = sensor->status.raw_gma;
        CCstatus.ImageLenc = sensor->status.lenc;

        // CCstatus.ImageSharpness = sensor->status.sharpness; // gibt -1 zurück, da es nicht unterstützt wird
        CCstatus.ImageDenoiseLevel = sensor->status.denoise;

        return ESP_OK;
    }
    else
    {
        return ESP_FAIL;
    }
}

esp_err_t CCamera::setCCstatusToCFstatus(void)
{
    CFstatus.CamXclkFreqMhz = CCstatus.CamXclkFreqMhz;

    CFstatus.ImageFrameSize = CCstatus.ImageFrameSize;

    CFstatus.ImageContrast = CCstatus.ImageContrast;
    CFstatus.ImageBrightness = CCstatus.ImageBrightness;
    CFstatus.ImageSaturation = CCstatus.ImageSaturation;

    CFstatus.ImageQuality = CCstatus.ImageQuality;

    CFstatus.ImageGainceiling = CCstatus.ImageGainceiling;

    CFstatus.ImageAgc = CCstatus.ImageAgc;
    CFstatus.ImageAec = CCstatus.ImageAec;
    CFstatus.ImageHmirror = CCstatus.ImageHmirror;
    CFstatus.ImageVflip = CCstatus.ImageVflip;

    CFstatus.ImageAwb = CCstatus.ImageAwb;
    CFstatus.ImageAec2 = CCstatus.ImageAec2;
    CFstatus.ImageAecValue = CCstatus.ImageAecValue;
    CFstatus.ImageSpecialEffect = CCstatus.ImageSpecialEffect;
    CFstatus.ImageWbMode = CCstatus.ImageWbMode;
    CFstatus.ImageAeLevel = CCstatus.ImageAeLevel;

    CFstatus.ImageDcw = CCstatus.ImageDcw;
    CFstatus.ImageBpc = CCstatus.ImageBpc;
    CFstatus.ImageWpc = CCstatus.ImageWpc;
    CFstatus.ImageAwbGain = CCstatus.ImageAwbGain;
    CFstatus.ImageAgcGain = CCstatus.ImageAgcGain;

    CFstatus.ImageRawGma = CCstatus.ImageRawGma;
    CFstatus.ImageLenc = CCstatus.ImageLenc;

    CFstatus.ImageSharpness = CCstatus.ImageSharpness;
    CFstatus.ImageAutoSharpness = CCstatus.ImageAutoSharpness;

    CFstatus.ImageDenoiseLevel = CCstatus.ImageDenoiseLevel;

    CFstatus.ImageLedIntensity = CCstatus.ImageLedIntensity;

    CFstatus.ImageZoomEnabled = CCstatus.ImageZoomEnabled;
    CFstatus.ImageZoomOffsetX = CCstatus.ImageZoomOffsetX;
    CFstatus.ImageZoomOffsetY = CCstatus.ImageZoomOffsetY;
    CFstatus.ImageZoomSize = CCstatus.ImageZoomSize;

    CFstatus.WaitBeforePicture = CCstatus.WaitBeforePicture;

    return ESP_OK;
}

esp_err_t CCamera::setCFstatusToCCstatus(void)
{
    CCstatus.CamXclkFreqMhz = CFstatus.CamXclkFreqMhz;

    CCstatus.ImageFrameSize = CFstatus.ImageFrameSize;

    CCstatus.ImageContrast = CFstatus.ImageContrast;
    CCstatus.ImageBrightness = CFstatus.ImageBrightness;
    CCstatus.ImageSaturation = CFstatus.ImageSaturation;

    CCstatus.ImageQuality = CFstatus.ImageQuality;

    CCstatus.ImageGainceiling = CFstatus.ImageGainceiling;

    CCstatus.ImageAgc = CFstatus.ImageAgc;
    CCstatus.ImageAec = CFstatus.ImageAec;
    CCstatus.ImageHmirror = CFstatus.ImageHmirror;
    CCstatus.ImageVflip = CFstatus.ImageVflip;

    CCstatus.ImageAwb = CFstatus.ImageAwb;
    CCstatus.ImageAec2 = CFstatus.ImageAec2;
    CCstatus.ImageAecValue = CFstatus.ImageAecValue;
    CCstatus.ImageSpecialEffect = CFstatus.ImageSpecialEffect;
    CCstatus.ImageWbMode = CFstatus.ImageWbMode;
    CCstatus.ImageAeLevel = CFstatus.ImageAeLevel;

    CCstatus.ImageDcw = CFstatus.ImageDcw;
    CCstatus.ImageBpc = CFstatus.ImageBpc;
    CCstatus.ImageWpc = CFstatus.ImageWpc;
    CCstatus.ImageAwbGain = CFstatus.ImageAwbGain;
    CCstatus.ImageAgcGain = CFstatus.ImageAgcGain;

    CCstatus.ImageRawGma = CFstatus.ImageRawGma;
    CCstatus.ImageLenc = CFstatus.ImageLenc;

    CCstatus.ImageSharpness = CFstatus.ImageSharpness;
    CCstatus.ImageAutoSharpness = CFstatus.ImageAutoSharpness;

    CCstatus.ImageDenoiseLevel = CFstatus.ImageDenoiseLevel;

    CCstatus.ImageLedIntensity = CFstatus.ImageLedIntensity;

    CCstatus.ImageZoomEnabled = CFstatus.ImageZoomEnabled;
    CCstatus.ImageZoomOffsetX = CFstatus.ImageZoomOffsetX;
    CCstatus.ImageZoomOffsetY = CFstatus.ImageZoomOffsetY;
    CCstatus.ImageZoomSize = CFstatus.ImageZoomSize;

    CCstatus.WaitBeforePicture = CFstatus.WaitBeforePicture;

    return ESP_OK;
}

int CCamera::CheckCamSettingsChanged(void)
{
    int ret = 0;
	
    // wenn die Kameraeinstellungen durch Erstellen eines neuen Referenzbildes verändert wurden, müssen sie neu gesetzt werden
    if (Camera.CamSettingsChanged)
    {
        if (Camera.CamTempImage)
        {
            Camera.setSensorDatenFromCFstatus(); // CFstatus >>> Kamera
            Camera.SetQualityZoomSize(CFstatus.ImageQuality, CFstatus.ImageFrameSize, CFstatus.ImageZoomEnabled, CFstatus.ImageZoomOffsetX, CFstatus.ImageZoomOffsetY, CFstatus.ImageZoomSize, CFstatus.ImageVflip);
            Camera.LedIntensity = CFstatus.ImageLedIntensity;
            Camera.CamTempImage = false;
        }
	    else
	    {
            Camera.setSensorDatenFromCCstatus(); // CCstatus >>> Kamera
            Camera.SetQualityZoomSize(CCstatus.ImageQuality, CCstatus.ImageFrameSize, CCstatus.ImageZoomEnabled, CCstatus.ImageZoomOffsetX, CCstatus.ImageZoomOffsetY, CCstatus.ImageZoomSize, CCstatus.ImageVflip);
            Camera.LedIntensity = CCstatus.ImageLedIntensity;
            Camera.CamSettingsChanged = false;
        }
    }
	
    return ret;
}

// on the OV5640, gainceiling must be set with the real value (x2>>>gainceilingLevel = 2, .... x128>>>gainceilingLevel = 128)
int CCamera::SetCamGainceiling(sensor_t *sensor, gainceiling_t gainceilingLevel)
{
    int ret = 0;

    if (Camera.CamSensor_id == OV2640_PID)
    {
        ret = sensor->set_gainceiling(sensor, gainceilingLevel); // Image gain (GAINCEILING_x2, x4, x8, x16, x32, x64 or x128)
    }
    else
    {
        int _level = (1 << ((int)gainceilingLevel + 1));
        ret = sensor->set_reg(sensor, 0x3A18, 0xFF, (_level >> 8) & 3) || sensor->set_reg(sensor, 0x3A19, 0xFF, _level & 0xFF);

        if (ret == 0)
        {
            // ESP_LOGD(TAG, "Set gainceiling to: %d", gainceilingLevel);
            sensor->status.gainceiling = gainceilingLevel;
        }
    }

    return ret;
}

int CCamera::SetCamSharpness(sensor_t *sensor, bool autoSharpnessEnabled, int sharpnessLevel)
{
    int ret = 0;

    if (Camera.CamSensor_id == OV2640_PID)
    {
        // The OV2640 does not officially support sharpness, so the detour is made with the ov2640_sharpness.cpp.
        if (autoSharpnessEnabled)
        {
            ret = ov2640_enable_auto_sharpness(sensor);
        }
        else
        {
            ret = ov2640_set_sharpness(sensor, sharpnessLevel);
        }
    }
    else
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
	
    return ret;
}

int CCamera::SetCamSpecialEffect(sensor_t *sensor, int specialEffect)
{
    int ret = 0;

    if (Camera.CamSensor_id == OV2640_PID)
    {
        ret = ov2640_set_special_effect(sensor, specialEffect);
    }
    else
    {
        ret = sensor->set_special_effect(sensor, specialEffect);
    }
	
    return ret;
}

int CCamera::SetCamContrastBrightness(sensor_t *sensor, int _contrast, int _brightness)
{
    int ret = 0;

    if (Camera.CamSensor_id == OV2640_PID)
    {
        ret = ov2640_set_contrast_brightness(sensor, _contrast, _brightness);
    }
    else
    {
        ret = sensor->set_contrast(sensor, _contrast);     // -2 to 2
        ret |= sensor->set_brightness(sensor, _brightness); // -2 to 2
    }
	
    return ret;
}

// - It always zooms to the image center when offsets are zero
// - if imageSize = 0 then the image is not zoomed
// - if imageSize = max value, then the image is fully zoomed in
// - a zoom step is >>> Width + 32 px / Height + 24 px
void CCamera::SanitizeZoomParams(int imageSize, int frameSizeX, int frameSizeY, int &imageWidth, int &imageHeight, int &zoomOffsetX, int &zoomOffsetY)
{
    // for OV2640, This works only if the aspect ratio of 4:3 is preserved in the window size.
    // use only values divisible by 8 without remainder
    if (Camera.CamTempImage)
    {
        imageWidth = CFstatus.ImageWidth + (imageSize * 4 * 8);
        imageHeight = CFstatus.ImageHeight + (imageSize * 3 * 8);
    }
    else
    {
        imageWidth = CCstatus.ImageWidth + (imageSize * 4 * 8);
        imageHeight = CCstatus.ImageHeight + (imageSize * 3 * 8);
    }

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

int CCamera::SetZoomSize(sensor_t *sensor, bool zoomEnabled, int zoomOffsetX, int zoomOffsetY, int imageSize, int imageVflip)
{
    int ret = 0;
	
    if (zoomEnabled)
    {
        int _imageSize_temp = 0;
		
        int _imageWidth = CCstatus.ImageWidth;
        int _imageHeight = CCstatus.ImageHeight;
        if (Camera.CamTempImage)
        {
            _imageWidth = CFstatus.ImageWidth;
            _imageHeight = CFstatus.ImageHeight;
        }
		
        int _offsetx = zoomOffsetX;
        int _offsety = zoomOffsetY;
        int frameSizeX = 1600;
        int frameSizeY = 1200;

        switch (Camera.CamSensor_id)
        {
            case OV5640_PID:
                frameSizeX = 2592;
                frameSizeY = 1944;
                // max imageSize = ((frameSizeX - CCstatus.ImageWidth) / 8 / 4) - 1
                // 59 = ((2560 - 640) / 8 / 4) - 1
                if (imageSize < 59)
                {
                    _imageSize_temp = (59 - imageSize);
                }
                break;

            case OV3660_PID:
                frameSizeX = 2048;
                frameSizeY = 1536;
                // max imageSize = ((frameSizeX - CCstatus.ImageWidth) / 8 / 4) -1
                // 43 = ((2048 - 640) / 8 / 4) - 1
                if (imageSize < 43)
                {
                    _imageSize_temp = (43 - imageSize);
                }
                break;

            case OV2640_PID:
                frameSizeX = 1600;
                frameSizeY = 1200;
                // max imageSize = ((frameSizeX - CCstatus.ImageWidth) / 8 / 4) -1
                // 29 = ((1600 - 640) / 8 / 4) - 1
                if (imageSize < 29)
                {
                    _imageSize_temp = (29 - imageSize);
                }
                break;

            default:
                // do nothing
                break;
        }
		
        SanitizeZoomParams(_imageSize_temp, frameSizeX, frameSizeY, _imageWidth, _imageHeight, _offsetx, _offsety);

        if (Camera.CamTempImage)
        {
            ret = SetCamWindow(sensor, frameSizeX, frameSizeY, _offsetx, _offsety, _imageWidth, _imageHeight, CFstatus.ImageWidth, CFstatus.ImageHeight, imageVflip);
        }
        else
        {
            ret = SetCamWindow(sensor, frameSizeX, frameSizeY, _offsetx, _offsety, _imageWidth, _imageHeight, CCstatus.ImageWidth, CCstatus.ImageHeight, imageVflip);
        }
    }
    else
    {
        if (Camera.CamTempImage)
        {
            ret = sensor->set_framesize(sensor, CFstatus.ImageFrameSize);
        }
        else
        {
            ret = sensor->set_framesize(sensor, CCstatus.ImageFrameSize);
        }
    }

    return ret;
}

int CCamera::SetQualityZoomSize(int qual, framesize_t resol, bool zoomEnabled, int zoomOffsetX, int zoomOffsetY, int imageSize, int imageVflip)
{
    int ret = 0;

    SetImageWidthHeightFromResolution(resol);

    sensor_t *sensor = esp_camera_sensor_get();
    if (sensor != NULL)
    {
        ret = sensor->set_quality(sensor, qual);
        ret |= SetZoomSize(sensor, zoomEnabled, zoomOffsetX, zoomOffsetY, imageSize, imageVflip);
    }
    else
    {
        ret = -1;
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "SetQualityZoomSize, Failed to get Cam control structure");
    }
	
    return ret;
}

int CCamera::SetCamWindow(sensor_t *sensor, int frameSizeX, int frameSizeY, int xOffset, int yOffset, int xTotal, int yTotal, int xOutput, int yOutput, int imageVflip)
{
    int ret = 0;

    if (Camera.CamSensor_id == OV2640_PID)
    {
        ret = sensor->set_res_raw(sensor, 0, 0, 0, 0, xOffset, yOffset, xTotal, yTotal, xOutput, yOutput, false, false);
    }
    else
    {
        // for CAMERA_OV5640 and CAMERA_OV3660
        bool scale = !(xOutput == xTotal && yOutput == yTotal);
        bool binning = (xTotal >= (frameSizeX >> 1));

        if (imageVflip == true)
        {
            ret = sensor->set_res_raw(sensor, xOffset, yOffset, xOffset + xTotal - 1, yOffset + yTotal - 1, 0, 0, frameSizeX, frameSizeY, xOutput, yOutput, scale, binning);
        }
        else
        {
            ret = sensor->set_res_raw(sensor, xOffset, yOffset, xOffset + xTotal, yOffset + yTotal, 0, 0, frameSizeX, frameSizeY, xOutput, yOutput, scale, binning);
        }
    }
	
    return ret;
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

esp_err_t CCamera::CaptureToBasisImage(CImageBasis *_Image, int delay)
{
#ifdef DEBUG_DETAIL_ON
    LogFile.WriteHeapInfo("CaptureToBasisImage - Start");
#endif

    CheckCamSettingsChanged();

    _Image->EmptyImage(); // Delete previous stored raw image -> black image

    LEDOnOff(true); // Status-LED on

    if (delay > 0)
    {
        LightOnOff(true); // Flash-LED on
        const TickType_t xDelay = delay / portTICK_PERIOD_MS;
        vTaskDelay(xDelay);
    }

#ifdef DEBUG_DETAIL_ON
    LogFile.WriteHeapInfo("CaptureToBasisImage - After LightOn");
#endif

    camera_fb_t *fb = esp_camera_fb_get();
    esp_camera_fb_return(fb);
    fb = esp_camera_fb_get();

    if (!fb)
    {
        LEDOnOff(false);   // Status-LED off
        LightOnOff(false); // Flash-LED off

        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "is not working anymore (CaptureToBasisImage) - most probably caused "
                                                "by a hardware problem (instablility, ...). System will reboot.");
        // doReboot();
        return ESP_FAIL;
    }

    if (CCstatus.DemoMode)
    {
        // Use images stored on SD-Card instead of camera image
        /* Replace Framebuffer with image from SD-Card */
        loadNextDemoImage(fb);
    }

    CImageBasis *_zwImage = new CImageBasis("zwImage");

    if (_zwImage)
    {
        _zwImage->LoadFromMemory(fb->buf, fb->len);
    }
    else
    {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "CaptureToBasisImage: Can't allocate _zwImage");
    }

    esp_camera_fb_return(fb);

#ifdef DEBUG_DETAIL_ON
    LogFile.WriteHeapInfo("CaptureToBasisImage - After fb_get");
#endif

    LEDOnOff(false); // Status-LED off

    if (delay > 0)
    {
        LightOnOff(false); // Flash-LED off
    }

#ifdef DEBUG_DETAIL_ON
    LogFile.WriteHeapInfo("CaptureToBasisImage - After LoadFromMemory");
#endif

    if (_zwImage == NULL)
    {
        return ESP_OK;
    }

    stbi_uc *p_target;
    stbi_uc *p_source;
    int channels = 3;
    int width = CCstatus.ImageWidth;
    int height = CCstatus.ImageHeight;
    if (Camera.CamTempImage)
    {
        int width = CFstatus.ImageWidth;
        int height = CFstatus.ImageHeight;
    }

#ifdef DEBUG_DETAIL_ON
    std::string _zw = "Targetimage: " + std::to_string((int)_Image->rgb_image) + " Size: " + std::to_string(_Image->width) + ", " + std::to_string(_Image->height);
    _zw = _zw + " _zwImage: " + std::to_string((int)_zwImage->rgb_image) + " Size: " + std::to_string(_zwImage->width) + ", " + std::to_string(_zwImage->height);
    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, _zw);
#endif

    for (int x = 0; x < width; ++x)
    {
        for (int y = 0; y < height; ++y)
        {
            p_target = _Image->rgb_image + (channels * (y * width + x));
            p_source = _zwImage->rgb_image + (channels * (y * width + x));

            for (int c = 0; c < channels; c++)
            {
                p_target[c] = p_source[c];
            }
        }
    }

    delete _zwImage;

#ifdef DEBUG_DETAIL_ON
    LogFile.WriteHeapInfo("CaptureToBasisImage - Done");
#endif

    return ESP_OK;
}

esp_err_t CCamera::CaptureToFile(std::string nm, int delay)
{
    CheckCamSettingsChanged();

    LEDOnOff(true); // Status-LED on

    if (delay > 0)
    {
        LightOnOff(true); // Flash-LED on
        const TickType_t xDelay = delay / portTICK_PERIOD_MS;
        vTaskDelay(xDelay);
    }

    camera_fb_t *fb = esp_camera_fb_get();
    esp_camera_fb_return(fb);
    fb = esp_camera_fb_get();

    if (!fb)
    {
        LEDOnOff(false);   // Status-LED off
        LightOnOff(false); // Flash-LED off
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "CaptureToFile: Capture Failed. "
                                                "Check camera module and/or proper electrical connection");
        // doReboot();
        return ESP_FAIL;
    }

    LEDOnOff(false); // Status-LED off

#ifdef DEBUG_DETAIL_ON
    ESP_LOGD(TAG, "w %d, h %d, size %d", fb->width, fb->height, fb->len);
#endif

    nm = FormatFileName(nm);

#ifdef DEBUG_DETAIL_ON
    ESP_LOGD(TAG, "Save Camera to: %s", nm.c_str());
#endif

    string ftype = toUpper(getFileType(nm));

#ifdef DEBUG_DETAIL_ON
    ESP_LOGD(TAG, "Filetype: %s", ftype.c_str());
#endif

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
            bool jpeg_converted = false;
            if (Camera.CamTempImage)
            {
                jpeg_converted = frame2jpg(fb, CFstatus.ImageQuality, &buf, &buf_len);
            }
            else
            {
                jpeg_converted = frame2jpg(fb, CCstatus.ImageQuality, &buf, &buf_len);
            }
			
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

    esp_camera_fb_return(fb);

    if (delay > 0)
    {
        LightOnOff(false); // Flash-LED off
    }

    return ESP_OK;
}

esp_err_t CCamera::CaptureToHTTP(httpd_req_t *req, int delay)
{
    esp_err_t res = ESP_OK;
    size_t fb_len = 0;
    int64_t fr_start = esp_timer_get_time();

    CheckCamSettingsChanged();

    LEDOnOff(true); // Status-LED on

    if (delay > 0)
    {
        LightOnOff(true); // Flash-LED on
        const TickType_t xDelay = delay / portTICK_PERIOD_MS;
        vTaskDelay(xDelay);
    }

    camera_fb_t *fb = esp_camera_fb_get();
    esp_camera_fb_return(fb);
    fb = esp_camera_fb_get();

    if (!fb)
    {
        LEDOnOff(false);   // Status-LED off
        LightOnOff(false); // Flash-LED off
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "CaptureToFile: Capture Failed. "
                                                "Check camera module and/or proper electrical connection");
        httpd_resp_send_500(req);
        // doReboot();
        return ESP_FAIL;
    }

    LEDOnOff(false); // Status-LED off
    res = httpd_resp_set_type(req, "image/jpeg");

    if (res == ESP_OK)
    {
        res = httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=raw.jpg");
    }

    if (res == ESP_OK)
    {
        if (CCstatus.DemoMode)
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
                jpg_chunking_t jchunk = {req, 0};
                res = frame2jpg_cb(fb, 80, jpg_encode_stream, &jchunk) ? ESP_OK : ESP_FAIL;
                httpd_resp_send_chunk(req, NULL, 0);
                fb_len = jchunk.len;
            }
        }
    }

    esp_camera_fb_return(fb);
    int64_t fr_end = esp_timer_get_time();

    ESP_LOGI(TAG, "JPG: %dKB %dms", (int)(fb_len / 1024), (int)((fr_end - fr_start) / 1000));

    if (delay > 0)
    {
        LightOnOff(false); // Flash-LED off
    }

    return res;
}

esp_err_t CCamera::CaptureToStream(httpd_req_t *req, bool FlashlightOn)
{
    esp_err_t res = ESP_OK;
    size_t fb_len = 0;
    int64_t fr_start;
    char *part_buf[64];

    LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Live stream started");

    CheckCamSettingsChanged();

    if (FlashlightOn)
    {
        LEDOnOff(true);   // Status-LED on
        LightOnOff(true); // Flash-LED on
    }

    httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
    httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));

    while (1)
    {
        fr_start = esp_timer_get_time();
        camera_fb_t *fb = esp_camera_fb_get();
        esp_camera_fb_return(fb);
        fb = esp_camera_fb_get();

        if (!fb)
        {
            LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "CaptureToStream: Camera framebuffer not available");
            break;
        }

        fb_len = fb->len;

        if (res == ESP_OK)
        {
            size_t hlen = snprintf((char *)part_buf, sizeof(part_buf), _STREAM_PART, fb_len);
            res = httpd_resp_send_chunk(req, (const char *)part_buf, hlen);
        }

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
        ESP_LOGD(TAG, "JPG: %dKB %dms", (int)(fb_len / 1024), (int)((fr_end - fr_start) / 1000));

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

    LEDOnOff(false);   // Status-LED off
    LightOnOff(false); // Flash-LED off

    LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Live stream stopped");

    return res;
}

void CCamera::LightOnOff(bool status)
{
    GpioHandler *gpioHandler = gpio_handler_get();

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

void CCamera::SetImageWidthHeightFromResolution(framesize_t resol)
{
    int _ImageWidth = 640;
    int _ImageHeight = 480;
	
    if (resol == FRAMESIZE_QVGA)
    {
        _ImageWidth = 320;
        _ImageHeight = 240;
    }
    else if (resol == FRAMESIZE_VGA)
    {
        _ImageWidth = 640;
        _ImageHeight = 480;
    }
    else if (resol == FRAMESIZE_SVGA)
    {
        _ImageWidth = 800;
        _ImageHeight = 600;
    }
    else if (resol == FRAMESIZE_XGA)
    {
        _ImageWidth = 1024;
        _ImageHeight = 768;
    }
    else if (resol == FRAMESIZE_HD)
    {
        _ImageWidth = 1280;
        _ImageHeight = 720;
    }
    else if (resol == FRAMESIZE_SXGA)
    {
        _ImageWidth = 1280;
        _ImageHeight = 1024;
    }
    else if (resol == FRAMESIZE_UXGA)
    {
        _ImageWidth = 1600;
        _ImageHeight = 1200;
    }
    else if (resol == FRAMESIZE_QXGA)
    {
        _ImageWidth = 2048;
        _ImageHeight = 1536;
    }
    else if (resol == FRAMESIZE_WQXGA)
    {
        _ImageWidth = 2560;
        _ImageHeight = 1600;
    }
    else if (resol == FRAMESIZE_QSXGA)
    {
        _ImageWidth = 2560;
        _ImageHeight = 1920;
    }

    if (Camera.CamTempImage)
    {
        CFstatus.ImageWidth = _ImageWidth;
        CFstatus.ImageHeight = _ImageHeight;
    }
    else
    {
        CCstatus.ImageWidth = _ImageWidth;
        CCstatus.ImageHeight = _ImageHeight;
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

    CCstatus.DemoMode = true;
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
