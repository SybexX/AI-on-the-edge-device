#include "ClassControllCamera.h"
#include "ClassLogFile.h"

#include <stdio.h>
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"

#include "Helper.h"
#include "statusled.h"
#include "CImageBasis.h"
#include "psram.h"

#include "server_ota.h"
#include "server_GPIO.h"

#include "../../include/defines.h"

#include <esp_event.h>
#include <esp_log.h>
#include <esp_system.h>
#include <nvs_flash.h>
#include <sys/param.h>
#include <string.h>
#include <limits.h>
#include <new>
#include <sys/stat.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "server_camera.h"
#include "esp_camera.h"
#include "esp_heap_caps.h"

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

#define DEMO_IMAGE_SIZE 30000 // Max size of demo image in bytes

// Camera module bus communications frequency.
// Originally: config.xclk_freq_mhz = 20000000, but this lead to visual artifacts on many modules.
// See https://github.com/espressif/esp32-camera/issues/150#issuecomment-726473652 et al.
#if !defined(XCLK_FREQ_MHZ)
// int xclk = 8;
int xclk = 20; // Orginal value
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

	CCstatus.ImageQuality = camera_config.jpeg_quality;
    CCstatus.ImageFrameSize = camera_config.frame_size;
	SetLEDIntensity(LedIntensity);
    CCstatus.WaitBeforePicture = 2;

    ledc_init();
}

esp_err_t CCamera::InitCam(void)
{
    ESP_LOGD(TAG, "Init Camera");

    // De-init in case it was already initialized
    PowerResetCamera();
    vTaskDelay(pdMS_TO_TICKS(200));
	
    // initialize the camera
    esp_err_t err = esp_camera_init(&camera_config);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Camera Init Failed");
        return err;
    }

    CCstatus.CameraInitSuccessful = true;

    // Get a reference to the sensor
    sensor_t *s = esp_camera_sensor_get();
    if (s != NULL)
    {
        CCstatus.CamSensor_id = s->id.PID;

        // Dump camera module, warn for unsupported modules.
        switch (CCstatus.CamSensor_id)
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
            CCstatus.CameraInitSuccessful = false;
        }
    }

    if (CCstatus.CameraInitSuccessful)
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
    // CCstatus.ImageLedIntensity = (int)(std::min(std::max((float)0, _intrel), (float)100) / 100 * 8191)
    Camera.LedIntensity = (int)((float)(std::min(std::max(0, _intrel), 100)) / 100 * 8191);
    ESP_LOGD(TAG, "Set led_intensity to %i of 8191", Camera.LedIntensity);
    return Camera.LedIntensity;
}

bool CCamera::getCameraInitSuccessful(void)
{
    return CCstatus.CameraInitSuccessful;
}

esp_err_t CCamera::setSensorDatenFromCCstatus(void)
{
    sensor_t *s = esp_camera_sensor_get();

    if (s != NULL)
    {
        s->set_framesize(s, CCstatus.ImageFrameSize);
		
        // s->set_contrast(s, CCstatus.ImageContrast);     // -2 to 2
        // s->set_brightness(s, CCstatus.ImageBrightness); // -2 to 2
        SetCamContrastBrightness(s, CCstatus.ImageContrast, CCstatus.ImageBrightness);
		
        s->set_saturation(s, CCstatus.ImageSaturation); // -2 to 2

        s->set_quality(s, CCstatus.ImageQuality); // 0 - 63
		
        // s->set_gainceiling(s, CCstatus.ImageGainceiling); // Image gain (GAINCEILING_x2, x4, x8, x16, x32, x64 or x128)
        SetCamGainceiling(s, CCstatus.ImageGainceiling);
		
        s->set_gain_ctrl(s, CCstatus.ImageAgc);     // 0 = disable , 1 = enable
        s->set_exposure_ctrl(s, CCstatus.ImageAec); // 0 = disable , 1 = enable
        s->set_hmirror(s, CCstatus.ImageHmirror); // 0 = disable , 1 = enable
        s->set_vflip(s, CCstatus.ImageVflip);     // 0 = disable , 1 = enable
		
        s->set_whitebal(s, CCstatus.ImageAwb);     // 0 = disable , 1 = enable
        s->set_aec2(s, CCstatus.ImageAec2);       // 0 = disable , 1 = enable
        s->set_aec_value(s, CCstatus.ImageAecValue); // 0 to 1200
        // s->set_special_effect(s, CCstatus.ImageSpecialEffect); // 0 to 6 (0 - No Effect, 1 - Negative, 2 - Grayscale, 3 - Red Tint, 4 - Green Tint, 5 - Blue Tint, 6 - Sepia)
        SetCamSpecialEffect(s, CCstatus.ImageSpecialEffect);
        s->set_wb_mode(s, CCstatus.ImageWbMode);               // 0 to 4 - if awb_gain enabled (0 - Auto, 1 - Sunny, 2 - Cloudy, 3 - Office, 4 - Home)
        s->set_ae_level(s, CCstatus.ImageAeLevel);   // -2 to 2
		
        s->set_dcw(s, CCstatus.ImageDcw); // 0 = disable , 1 = enable
        s->set_bpc(s, CCstatus.ImageBpc); // 0 = disable , 1 = enable
        s->set_wpc(s, CCstatus.ImageWpc); // 0 = disable , 1 = enable
        s->set_awb_gain(s, CCstatus.ImageAwbGain); // 0 = disable , 1 = enable
        s->set_agc_gain(s, CCstatus.ImageAgcGain);   // 0 to 30
		
        s->set_raw_gma(s, CCstatus.ImageRawGma); // 0 = disable , 1 = enable
        s->set_lenc(s, CCstatus.ImageLenc);         // 0 = disable , 1 = enable

        // s->set_sharpness(s, CCstatus.ImageSharpness);   // auto-sharpness is not officially supported, default to 0
        SetCamSharpness(CCstatus.ImageAutoSharpness, CCstatus.ImageSharpness);
        s->set_denoise(s, CCstatus.ImageDenoiseLevel); // The OV2640 does not support it, OV3660 and OV5640 (0 to 8)

        TickType_t cam_xDelay = 100 / portTICK_PERIOD_MS;
        vTaskDelay(cam_xDelay);

        return ESP_OK;
    }
    else
    {
        return ESP_FAIL;
    }
}

esp_err_t CCamera::getSensorDatenToCCstatus(void)
{
    sensor_t *s = esp_camera_sensor_get();

    if (s != NULL)
    {
        CCstatus.CamSensor_id = s->id.PID;

        CCstatus.ImageFrameSize = (framesize_t)s->status.framesize;
		
        CCstatus.ImageContrast = s->status.contrast;
        CCstatus.ImageBrightness = s->status.brightness;
        CCstatus.ImageSaturation = s->status.saturation;
		
        CCstatus.ImageQuality = s->status.quality;
		
        CCstatus.ImageGainceiling = (gainceiling_t)s->status.gainceiling;

        CCstatus.ImageAgc = s->status.agc;
        CCstatus.ImageAec = s->status.aec;
        CCstatus.ImageHmirror = s->status.hmirror;
        CCstatus.ImageVflip = s->status.vflip;
		
        CCstatus.ImageAwb = s->status.awb;
        CCstatus.ImageAec2 = s->status.aec2;
        CCstatus.ImageAecValue = s->status.aec_value;
        CCstatus.ImageSpecialEffect = s->status.special_effect;
        CCstatus.ImageWbMode = s->status.wb_mode;
        CCstatus.ImageAeLevel = s->status.ae_level;
		
        CCstatus.ImageDcw = s->status.dcw;
        CCstatus.ImageBpc = s->status.bpc;
        CCstatus.ImageWpc = s->status.wpc;
        CCstatus.ImageAwbGain = s->status.awb_gain;
        CCstatus.ImageAgcGain = s->status.agc_gain;
		
        CCstatus.ImageRawGma = s->status.raw_gma;
        CCstatus.ImageLenc = s->status.lenc;

        // CCstatus.ImageSharpness = s->status.sharpness; // gibt -1 zurück, da es nicht unterstützt wird
        CCstatus.ImageDenoiseLevel = s->status.denoise;

        return ESP_OK;
    }
    else
    {
        return ESP_FAIL;
    }
}

// on the OV5640, gainceiling must be set with the real value (x2>>>gainceilingLevel = 2, .... x128>>>gainceilingLevel = 128)
int CCamera::SetCamGainceiling(sensor_t *s, gainceiling_t gainceilingLevel)
{
	int ret = 0;
		
    if (CCstatus.CamSensor_id == OV2640_PID)
    {
        ret = s->set_gainceiling(s, gainceilingLevel); // Image gain (GAINCEILING_x2, x4, x8, x16, x32, x64 or x128)
    }
    else
    {
        int _level = (1 << ((int)gainceilingLevel + 1));

        ret = s->set_reg(s, 0x3A18, 0xFF, (_level >> 8) & 3) || s->set_reg(s, 0x3A19, 0xFF, _level & 0xFF);

        if (ret == 0)
        {
            // ESP_LOGD(TAG, "Set gainceiling to: %d", gainceilingLevel);
            s->status.gainceiling = gainceilingLevel;
        }
    }

    return ret;
}

void CCamera::SetCamSharpness(bool autoSharpnessEnabled, int sharpnessLevel)
{
    sensor_t *s = esp_camera_sensor_get();

    if (s != NULL)
    {
        if (CCstatus.CamSensor_id == OV2640_PID)
        {
            sharpnessLevel = min(2, max(-2, sharpnessLevel));
            // The OV2640 does not officially support sharpness, so the detour is made with the ov2640_sharpness.cpp.
            if (autoSharpnessEnabled)
            {
                ov2640_enable_auto_sharpness(s);
            }
            else
            {
                ov2640_set_sharpness(s, sharpnessLevel);
            }
        }
        else
        {
            sharpnessLevel = min(3, max(-3, sharpnessLevel));
            // for CAMERA_OV5640 and CAMERA_OV3660
            if (autoSharpnessEnabled)
            {
                // autoSharpness is not supported, default to zero
                s->set_sharpness(s, 0);
            }
            else
            {
                s->set_sharpness(s, sharpnessLevel);
            }
        }
    }
    else
    {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "SetCamSharpness, Failed to get Cam control structure");
    }
}

void CCamera::SetCamSpecialEffect(sensor_t *s, int specialEffect)
{
    if (CCstatus.CamSensor_id == OV2640_PID)
    {
        ov2640_set_special_effect(s, specialEffect);
    }
    else
    {
        s->set_special_effect(s, specialEffect);
    }
}

void CCamera::SetCamContrastBrightness(sensor_t *s, int _contrast, int _brightness)
{
    if (CCstatus.CamSensor_id == OV2640_PID)
    {
        ov2640_set_contrast_brightness(s, _contrast, _brightness);
    }
    else
    {
        s->set_contrast(s, _contrast);     // -2 to 2
        s->set_brightness(s, _brightness); // -2 to 2
    }
}

// - It always zooms to the image center when offsets are zero
// - if imageSize = 0 then the image is not zoomed
// - if imageSize = max value, then the image is fully zoomed in
// - a zoom step is >>> Width + 32 px / Height + 24 px
void CCamera::SanitizeZoomParams(int imageSize, int frameSizeX, int frameSizeY, int &imageWidth, int &imageHeight, int &zoomOffsetX, int &zoomOffsetY)
{
    // for OV2640, This works only if the aspect ratio of 4:3 is preserved in the window size.
    // use only values divisible by 8 without remainder
    imageWidth = CCstatus.ImageWidth + (imageSize * 4 * 8);
    imageHeight = CCstatus.ImageHeight + (imageSize * 3 * 8);

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

void CCamera::SetZoomSize(bool zoomEnabled, int zoomOffsetX, int zoomOffsetY, int imageSize, int imageVflip)
{
    sensor_t *s = esp_camera_sensor_get();

    if (s != NULL)
    {
        if (zoomEnabled)
        {
            int _imageSize_temp = 0;
            int _imageWidth = CCstatus.ImageWidth;
            int _imageHeight = CCstatus.ImageHeight;
            int _offsetx = zoomOffsetX;
            int _offsety = zoomOffsetY;
            int frameSizeX;
            int frameSizeY;

            switch (CCstatus.CamSensor_id)
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
                SanitizeZoomParams(_imageSize_temp, frameSizeX, frameSizeY, _imageWidth, _imageHeight, _offsetx, _offsety);
                SetCamWindow(s, frameSizeX, frameSizeY, _offsetx, _offsety, _imageWidth, _imageHeight, CCstatus.ImageWidth, CCstatus.ImageHeight, imageVflip);
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
                SanitizeZoomParams(_imageSize_temp, frameSizeX, frameSizeY, _imageWidth, _imageHeight, _offsetx, _offsety);
                SetCamWindow(s, frameSizeX, frameSizeY, _offsetx, _offsety, _imageWidth, _imageHeight, CCstatus.ImageWidth, CCstatus.ImageHeight, imageVflip);
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
                SanitizeZoomParams(_imageSize_temp, frameSizeX, frameSizeY, _imageWidth, _imageHeight, _offsetx, _offsety);
                SetCamWindow(s, frameSizeX, frameSizeY, _offsetx, _offsety, _imageWidth, _imageHeight, CCstatus.ImageWidth, CCstatus.ImageHeight, imageVflip);
                break;

            default:
                // do nothing
                break;
            }
        }
        else
        {
            s->set_framesize(s, CCstatus.ImageFrameSize);
        }
    }
}

void CCamera::SetQualityZoomSize(int qual, framesize_t resol, bool zoomEnabled, int zoomOffsetX, int zoomOffsetY, int imageSize, int imageVflip)
{
    sensor_t *s = esp_camera_sensor_get();

    // OV2640 has no lower limit on jpeg quality
    if (CCstatus.CamSensor_id == OV5640_PID)
    {
        qual = min(63, max(8, qual));
    }

    SetImageWidthHeightFromResolution(resol);

    if (s != NULL)
    {
        s->set_quality(s, qual);
        SetZoomSize(zoomEnabled, zoomOffsetX, zoomOffsetY, imageSize, imageVflip);
    }
    else
    {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "SetQualityZoomSize, Failed to get Cam control structure");
    }
}

void CCamera::SetCamWindow(sensor_t *s, int frameSizeX, int frameSizeY, int xOffset, int yOffset, int xTotal, int yTotal, int xOutput, int yOutput, int imageVflip)
{
    if (CCstatus.CamSensor_id == OV2640_PID)
    {
        s->set_res_raw(s, 0, 0, 0, 0, xOffset, yOffset, xTotal, yTotal, xOutput, yOutput, false, false);
    }
    else
    {
        // for CAMERA_OV5640 and CAMERA_OV3660
        bool scale = !(xOutput == xTotal && yOutput == yTotal);
        bool binning = (xTotal >= (frameSizeX >> 1));

        if (imageVflip == true)
        {
            s->set_res_raw(s, xOffset, yOffset, xOffset + xTotal - 1, yOffset + yTotal - 1, 0, 0, frameSizeX, frameSizeY, xOutput, yOutput, scale, binning);
        }
        else
        {
            s->set_res_raw(s, xOffset, yOffset, xOffset + xTotal, yOffset + yTotal, 0, 0, frameSizeX, frameSizeY, xOutput, yOutput, scale, binning);
        }
    }
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
    if (_Image == NULL || !_Image->ImageOkay()) {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "CaptureToBasisImage: target image is unavailable");
        return ESP_FAIL;
    }

#ifdef DEBUG_DETAIL_ON
    LogFile.WriteHeapInfo("CaptureToBasisImage - Start");
#endif

    LEDOnOff(true);
    if (delay > 0) {
        LightOnOff(true);
        vTaskDelay(pdMS_TO_TICKS(delay));
    }

    camera_fb_t *warmup = esp_camera_fb_get();
    if (warmup != NULL) esp_camera_fb_return(warmup);
    camera_fb_t *fb = esp_camera_fb_get();
    if (fb == NULL) {
        LEDOnOff(false);
        LightOnOff(false);
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "CaptureToBasisImage: camera capture failed");
        return ESP_FAIL;
    }

    uint8_t *frameData = fb->buf;
    size_t frameLength = fb->len;
    uint8_t *demoData = NULL;
    bool demoBufferAllocated = false;
    if (CCstatus.DemoMode) {
        if (!loadNextDemoImage(&demoData, &frameLength)) {
            esp_camera_fb_return(fb);
            LEDOnOff(false);
            LightOnOff(false);
            LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "CaptureToBasisImage: could not load demo image");
            return ESP_FAIL;
        }
        frameData = demoData;
        demoBufferAllocated = true;
    }

    CImageBasis *tmpImage = new (std::nothrow) CImageBasis("tmpImage");
    bool decoded = tmpImage != NULL && frameLength <= static_cast<size_t>(INT_MAX) &&
                   tmpImage->LoadFromMemory(frameData, static_cast<int>(frameLength));
    if (demoBufferAllocated) free_psram_heap("Demo image frame", demoData);
    esp_camera_fb_return(fb);

    LEDOnOff(false);
    if (delay > 0) LightOnOff(false);

    if (!decoded || tmpImage == NULL || !tmpImage->ImageOkay()) {
        delete tmpImage;
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "CaptureToBasisImage: JPEG decode failed");
        LogFile.WriteHeapInfo("CaptureToBasisImage decode");
        return ESP_FAIL;
    }

    size_t expectedBytes = 0;
    if (_Image->width != tmpImage->width || _Image->height != tmpImage->height ||
        _Image->channels != tmpImage->channels || tmpImage->channels != 3 ||
        tmpImage->width <= 0 || tmpImage->height <= 0 ||
        static_cast<size_t>(tmpImage->width) > SIZE_MAX / static_cast<size_t>(tmpImage->height) ||
        static_cast<size_t>(tmpImage->width) * static_cast<size_t>(tmpImage->height) > SIZE_MAX / 3U) {
        delete tmpImage;
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "CaptureToBasisImage: decoded dimensions do not match target image");
        return ESP_FAIL;
    }
    expectedBytes = static_cast<size_t>(tmpImage->width) * static_cast<size_t>(tmpImage->height) * 3U;
    if (expectedBytes > static_cast<size_t>(INT_MAX)) {
        delete tmpImage;
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "CaptureToBasisImage: decoded image is too large");
        return ESP_FAIL;
    }

    uint8_t *source = tmpImage->RGBImageLock();
    if (source == NULL) {
        delete tmpImage;
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "CaptureToBasisImage: decoded image lock failed");
        return ESP_FAIL;
    }
    const bool copied = _Image->CopyFromMemory(source, static_cast<int>(expectedBytes));
    tmpImage->RGBImageRelease();
    delete tmpImage;

#ifdef DEBUG_DETAIL_ON
    LogFile.WriteHeapInfo("CaptureToBasisImage - Done");
#endif
    return copied ? ESP_OK : ESP_FAIL;
}

esp_err_t CCamera::CaptureToFile(std::string nm, int delay)
{
    string ftype;

    LEDOnOff(true); // Status-LED on

    if (delay > 0)
    {
        LightOnOff(true); // Flash-LED on
        const TickType_t xDelay = delay / portTICK_PERIOD_MS;
        vTaskDelay(xDelay);
    }

    camera_fb_t *warmup = esp_camera_fb_get();
    if (warmup != NULL) esp_camera_fb_return(warmup);
    camera_fb_t *fb = esp_camera_fb_get();

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

    ftype = toUpper(getFileType(nm));

#ifdef DEBUG_DETAIL_ON
    ESP_LOGD(TAG, "Filetype: %s", ftype.c_str());
#endif

    uint8_t *buf = NULL;
    size_t buf_len = 0;
    bool converted = false;
    bool captureOkay = false;

    if (ftype == "BMP") {
        converted = true;
        captureOkay = frame2bmp(fb, &buf, &buf_len) && buf != NULL && buf_len > 0;
    } else if (ftype == "JPG") {
        if (fb->format == PIXFORMAT_JPEG) {
            buf_len = fb->len;
            buf = fb->buf;
            captureOkay = (buf != NULL && buf_len > 0);
        } else {
            converted = true;
            captureOkay = frame2jpg(fb, CCstatus.ImageQuality, &buf, &buf_len) &&
                          buf != NULL && buf_len > 0;
            if (!captureOkay) ESP_LOGE(TAG, "JPEG compression failed");
        }
    }

    if (captureOkay) {
        FILE *fp = fopen(nm.c_str(), "wb");
        if (fp == NULL) {
            LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "CaptureToFile: Failed to open file " + nm);
            captureOkay = false;
        } else {
            const size_t written = fwrite(buf, 1, buf_len, fp);
            const int closeResult = fclose(fp);
            if (written != buf_len || closeResult != 0) {
                LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "CaptureToFile: incomplete write to " + nm);
                captureOkay = false;
            }
        }
    } else {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "CaptureToFile: unsupported image format or conversion failed");
    }

    if (converted && buf != NULL) free(buf);

    esp_camera_fb_return(fb);

    if (delay > 0)
    {
        LightOnOff(false); // Flash-LED off
    }

    return captureOkay ? ESP_OK : ESP_FAIL;
}

esp_err_t CCamera::CaptureToHTTP(httpd_req_t *req, int delay)
{
    esp_err_t res = ESP_OK;
    size_t fb_len = 0;
    int64_t fr_start = esp_timer_get_time();

    LEDOnOff(true); // Status-LED on

    if (delay > 0)
    {
        LightOnOff(true); // Flash-LED on
        const TickType_t xDelay = delay / portTICK_PERIOD_MS;
        vTaskDelay(xDelay);
    }

    camera_fb_t *warmup = esp_camera_fb_get();
    if (warmup != NULL) esp_camera_fb_return(warmup);
    camera_fb_t *fb = esp_camera_fb_get();

    if (!fb)
    {
        LEDOnOff(false);   // Status-LED off
        LightOnOff(false); // Flash-LED off
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "CaptureToFile: Capture Failed. "
                                                "Check camera module and/or proper electrical connection");
        httpd_resp_send_500(req);
        //        doReboot();

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
            LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "Using Demo image!");
            uint8_t *demoData = NULL;
            size_t demoLength = 0;
            if (loadNextDemoImage(&demoData, &demoLength)) {
                fb_len = demoLength;
                res = httpd_resp_send(req, (const char *)demoData, demoLength);
                free_psram_heap("Demo image HTTP frame", demoData);
            } else {
                LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "CaptureToHTTP: could not load demo image");
                res = ESP_FAIL;
            }
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
    char part_buf[64];

    // wenn die Kameraeinstellungen durch Erstellen eines neuen Referenzbildes verändert wurden, müssen sie neu gesetzt werden
    if (CFstatus.changedCameraSettings)
    {
        Camera.setSensorDatenFromCCstatus(); // CCstatus >>> Kamera
        Camera.SetQualityZoomSize(CCstatus.ImageQuality, CCstatus.ImageFrameSize, CCstatus.ImageZoomEnabled, CCstatus.ImageZoomOffsetX, CCstatus.ImageZoomOffsetY, CCstatus.ImageZoomSize, CCstatus.ImageVflip);
        Camera.LedIntensity = CCstatus.ImageLedIntensity;
        CFstatus.changedCameraSettings = false;
    }

    LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Live stream started");

    if (FlashlightOn)
    {
        LEDOnOff(true);   // Status-LED on
        LightOnOff(true); // Flash-LED on
    }

    // httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");  //stream is blocking web interface, only serving to local

    httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
    httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));

    while (1)
    {
        fr_start = esp_timer_get_time();
        camera_fb_t *warmup = esp_camera_fb_get();
        if (warmup != NULL) esp_camera_fb_return(warmup);
        camera_fb_t *fb = esp_camera_fb_get();

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


std::vector<std::string> demoFiles;

void CCamera::useDemoMode(void)
{
    char line[50];
    demoFiles.clear();
    FILE *fd = fopen("/sdcard/demo/files.txt", "r");
    if (fd == NULL) {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Can not start Demo mode: '/sdcard/demo/files.txt' is missing!");
        CCstatus.DemoMode = false;
        return;
    }
    while (fgets(line, sizeof(line), fd) != NULL) {
        size_t length = strlen(line);
        while (length > 0 && (line[length-1] == '\n' || line[length-1] == '\r')) line[--length] = '\0';
        if (length > 0) demoFiles.push_back(line);
    }
    fclose(fd);
    if (demoFiles.empty()) {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Can not start Demo mode: files.txt contains no image names");
        CCstatus.DemoMode = false;
        return;
    }
    LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Using Demo mode (" + std::to_string(demoFiles.size()) + " files) instead of real camera image!");
    for (const auto& file : demoFiles) LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, file);
    CCstatus.DemoMode = true;
}

bool CCamera::loadNextDemoImage(uint8_t **data, size_t *length)
{
    if (data == NULL || length == NULL || demoFiles.empty()) return false;
    *data = NULL;
    *length = 0;

    const size_t index = static_cast<size_t>(getCountFlowRounds()) % demoFiles.size();
    const std::string filename = "/sdcard/demo/" + demoFiles[index];
    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "Using " + filename + " as demo image");

    FILE *fp = fopen(filename.c_str(), "rb");
    if (fp == NULL) return false;
    const long fileSize = GetFileSize(filename);
    if (fileSize <= 0 || fileSize > DEMO_IMAGE_SIZE) {
        fclose(fp);
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Demo image has invalid size: " + std::to_string(fileSize));
        return false;
    }

    uint8_t *buffer = static_cast<uint8_t*>(malloc_psram_heap(
        "Demo image frame", static_cast<size_t>(fileSize), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (buffer == NULL) {
        fclose(fp);
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Demo image PSRAM allocation failed");
        return false;
    }

    const size_t readBytes = fread(buffer, 1, static_cast<size_t>(fileSize), fp);
    const bool readOkay = (readBytes == static_cast<size_t>(fileSize)) && (ferror(fp) == 0);
    fclose(fp);
    if (!readOkay) {
        free_psram_heap("Demo image frame (short read)", buffer);
        return false;
    }

    *data = buffer;
    *length = readBytes;
    return true;
}


long CCamera::GetFileSize(std::string filename)
{
    struct stat stat_buf;
    long rc = stat(filename.c_str(), &stat_buf);
    return rc == 0 ? stat_buf.st_size : -1;
}
