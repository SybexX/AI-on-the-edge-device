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

camera_controll_config_temp_t CCstatus;
CCamera Camera;

static const char *TAG = "CAM";

static int sensorValueOrDefault(int value, int fallback, int minimum, int maximum)
{
    return (value < minimum || value > maximum) ? fallback : value;
}

static int sensorBooleanOrDefault(int value, int fallback)
{
    return (value == 0 || value == 1) ? value : fallback;
}

static bool isSupportedFrameSize(int value)
{
    switch (static_cast<framesize_t>(value))
    {
    case FRAMESIZE_QVGA:
    case FRAMESIZE_VGA:
    case FRAMESIZE_SVGA:
    case FRAMESIZE_XGA:
    case FRAMESIZE_HD:
    case FRAMESIZE_SXGA:
    case FRAMESIZE_UXGA:
    case FRAMESIZE_QXGA:
    case FRAMESIZE_WQXGA:
    case FRAMESIZE_QSXGA:
        return true;
    default:
        return false;
    }
}

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
    .pin_sccb_sda = CAM_PIN_SIOD,
    .pin_sccb_scl = CAM_PIN_SIOC,

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
    .sccb_i2c_port = 0,               // unused when the SCCB pins are configured above
};

typedef struct
{
    httpd_req_t *req;
    size_t len;
} jpg_chunking_t;

// The camera driver and sensor register access are not safe to use concurrently.
// Recursive locking allows public methods to safely call other locked methods.
class ScopedCameraLock
{
public:
    explicit ScopedCameraLock(SemaphoreHandle_t mutex, TickType_t timeout = pdMS_TO_TICKS(5000))
        : m_mutex(mutex),
          m_acquired(mutex != NULL && xSemaphoreTakeRecursive(mutex, timeout) == pdTRUE)
    {
    }

    ~ScopedCameraLock()
    {
        if (m_acquired)
        {
            xSemaphoreGiveRecursive(m_mutex);
        }
    }

    bool acquired() const { return m_acquired; }

private:
    SemaphoreHandle_t m_mutex;
    bool m_acquired;
};

CCamera::CCamera(void)
    : m_cameraMutex(xSemaphoreCreateRecursiveMutex()),
      m_driverInitialized(false)
{
#ifdef DEBUG_DETAIL_ON
    ESP_LOGD(TAG, "CreateClassCamera");
#endif

    CCstatus.ImageQuality = camera_config.jpeg_quality;
    CCstatus.ImageFrameSize = camera_config.frame_size;
    CCstatus.CamXclkFreqMhz = xclk;
    CCstatus.ImageLedIntensity = SetLEDIntensity(LedIntensity);
    CCstatus.WaitBeforePicture = 2;
    SetImageWidthHeightFromResolution(CCstatus);

    ledc_init();
}

esp_err_t CCamera::InitCam(void)
{
    ScopedCameraLock cameraLock(m_cameraMutex);
    if (!cameraLock.acquired())
    {
        ESP_LOGE(TAG, "InitCam: camera mutex unavailable");
        CCstatus.CameraInitSuccessful = false;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGD(TAG, "Init Camera");
    CCstatus.CameraInitSuccessful = false;

    // Release an earlier driver instance before resetting and reinitializing.
    if (m_driverInitialized)
    {
        const esp_err_t deinitResult = esp_camera_deinit();
        if (deinitResult != ESP_OK)
        {
            ESP_LOGE(TAG, "Camera deinitialization failed: %s", esp_err_to_name(deinitResult));
            return deinitResult;
        }
        m_driverInitialized = false;
    }

    PowerResetCamera();
    vTaskDelay(pdMS_TO_TICKS(200));

    const esp_err_t err = esp_camera_init(&camera_config);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Camera Init Failed: %s", esp_err_to_name(err));
        return err;
    }
    m_driverInitialized = true;

    sensor_t *s = esp_camera_sensor_get();
    if (s == NULL)
    {
        ESP_LOGE(TAG, "Camera sensor handle is unavailable");
    }
    else
    {
        CCstatus.CamSensor_id = s->id.PID;
        switch (CCstatus.CamSensor_id)
        {
        case OV2640_PID:
            ESP_LOGI(TAG, "OV2640 camera module detected");
            CCstatus.CameraInitSuccessful = true;
            break;
        case OV3660_PID:
            ESP_LOGI(TAG, "OV3660 camera module detected");
            CCstatus.CameraInitSuccessful = true;
            break;
        case OV5640_PID:
            ESP_LOGI(TAG, "OV5640 camera module detected");
            CCstatus.CameraInitSuccessful = true;
            break;
        default:
            ESP_LOGE(TAG, "Camera module is unknown and not properly supported!");
            break;
        }
    }

    if (!CCstatus.CameraInitSuccessful)
    {
        const esp_err_t cleanupResult = esp_camera_deinit();
        if (cleanupResult != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to clean up unsupported camera: %s", esp_err_to_name(cleanupResult));
        }
        m_driverInitialized = (cleanupResult != ESP_OK);
        return ESP_FAIL;
    }

    return ESP_OK;
}

bool CCamera::testCamera(void)
{
    ScopedCameraLock cameraLock(m_cameraMutex);
    if (!cameraLock.acquired())
    {
        ESP_LOGW(TAG, "testCamera: camera mutex unavailable");
        return false;
    }

    camera_fb_t *fb = esp_camera_fb_get();
    if (fb == NULL)
    {
        ESP_LOGW(TAG, "testCamera: framebuffer capture failed");
        return false;
    }

    esp_camera_fb_return(fb);
    return true;
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
    ScopedCameraLock cameraLock(m_cameraMutex);
    return cameraLock.acquired() && CCstatus.CameraInitSuccessful;
}

esp_err_t CCamera::setSensorDatenFromCCstatus(void)
{
    return ApplySettings(CCstatus);
}

esp_err_t CCamera::ApplySettings(CameraSettings &settings)
{
    ScopedCameraLock cameraLock(m_cameraMutex);
    if (!cameraLock.acquired())
    {
        ESP_LOGW(TAG, "ApplySettings: camera mutex unavailable");
        return ESP_ERR_TIMEOUT;
    }
    if (!m_driverInitialized)
    {
        ESP_LOGW(TAG, "ApplySettings: camera driver is not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    sensor_t *s = esp_camera_sensor_get();
    if (s == NULL)
    {
        ESP_LOGE(TAG, "ApplySettings: camera sensor unavailable");
        return ESP_FAIL;
    }

    if (settings.CamXclkFreqMhz < 1 || settings.CamXclkFreqMhz > 20)
    {
        settings.CamXclkFreqMhz = xclk;
    }
    SetImageWidthHeightFromResolution(settings);

    int firstError = s->set_xclk(s, camera_config.ledc_timer, settings.CamXclkFreqMhz);
    const auto recordError = [&firstError](int result)
    {
        if (firstError == 0 && result != 0) firstError = result;
    };

    recordError(s->set_framesize(s, settings.ImageFrameSize));
    SetCamContrastBrightness(s, settings.ImageContrast, settings.ImageBrightness);
    recordError(s->set_saturation(s, settings.ImageSaturation));
    recordError(s->set_quality(s, settings.ImageQuality));
    recordError(SetCamGainceiling(s, settings.ImageGainceiling));

    recordError(s->set_gain_ctrl(s, settings.ImageAgc));
    recordError(s->set_exposure_ctrl(s, settings.ImageAec));
    recordError(s->set_hmirror(s, settings.ImageHmirror));
    recordError(s->set_vflip(s, settings.ImageVflip));
    recordError(s->set_whitebal(s, settings.ImageAwb));
    recordError(s->set_aec2(s, settings.ImageAec2));
    recordError(s->set_aec_value(s, settings.ImageAecValue));
    SetCamSpecialEffect(s, settings.ImageSpecialEffect);
    recordError(s->set_wb_mode(s, settings.ImageWbMode));
    recordError(s->set_ae_level(s, settings.ImageAeLevel));

    recordError(s->set_dcw(s, settings.ImageDcw));
    recordError(s->set_bpc(s, settings.ImageBpc));
    recordError(s->set_wpc(s, settings.ImageWpc));
    recordError(s->set_awb_gain(s, settings.ImageAwbGain));
    recordError(s->set_agc_gain(s, settings.ImageAgcGain));
    recordError(s->set_raw_gma(s, settings.ImageRawGma));
    recordError(s->set_lenc(s, settings.ImageLenc));
    SetCamSharpness(settings.ImageAutoSharpness, settings.ImageSharpness);
    recordError(s->set_denoise(s, settings.ImageDenoiseLevel));

    const esp_err_t geometryResult = SetQualityZoomSize(settings);
    if (geometryResult != ESP_OK) return geometryResult;

    LedIntensity = min(8191, max(0, settings.ImageLedIntensity));
    vTaskDelay(pdMS_TO_TICKS(100));
    return firstError == 0 ? ESP_OK : ESP_FAIL;
}

esp_err_t CCamera::RestoreCommittedSettingsIfNeeded(bool preserveTemporaryCapture)
{
    if (!CFstatus.changedCameraSettings || preserveTemporaryCapture)
    {
        return ESP_OK;
    }

    const esp_err_t result = ApplySettings(CCstatus);
    if (result == ESP_OK)
    {
        CFstatus.changedCameraSettings = false;
        CFstatus.previewCapturePending = false;
    }
    return result;
}

esp_err_t CCamera::getSensorDatenToCCstatus(void)
{
    ScopedCameraLock cameraLock(m_cameraMutex);
    if (!cameraLock.acquired())
    {
        ESP_LOGW(TAG, "getSensorDatenToCCstatus: camera mutex unavailable");
        return ESP_ERR_TIMEOUT;
    }

    sensor_t *s = esp_camera_sensor_get();
    if (s == NULL)
    {
        ESP_LOGE(TAG, "getSensorDatenToCCstatus: camera sensor unavailable");
        return ESP_FAIL;
    }

    CCstatus.CamSensor_id = s->id.PID;
    const int frameSize = static_cast<int>(s->status.framesize);
    CCstatus.ImageFrameSize = static_cast<framesize_t>(
        isSupportedFrameSize(frameSize) ? frameSize : static_cast<int>(FRAMESIZE_VGA));

    CCstatus.CamXclkFreqMhz = sensorValueOrDefault(
        static_cast<int>(s->xclk_freq_hz / 1000000U), xclk, 1, 20);
    CCstatus.ImageContrast = sensorValueOrDefault(s->status.contrast, 0, -2, 2);
    CCstatus.ImageBrightness = sensorValueOrDefault(s->status.brightness, 0, -2, 2);
    CCstatus.ImageSaturation = sensorValueOrDefault(s->status.saturation, 0, -2, 2);
    CCstatus.ImageQuality = sensorValueOrDefault(s->status.quality, 12, 6, 63);
    CCstatus.ImageGainceiling = static_cast<gainceiling_t>(
        sensorValueOrDefault(static_cast<int>(s->status.gainceiling), 0, 0, 6));

    CCstatus.ImageAgc = sensorBooleanOrDefault(s->status.agc, 1);
    CCstatus.ImageAec = sensorBooleanOrDefault(s->status.aec, 1);
    CCstatus.ImageHmirror = sensorBooleanOrDefault(s->status.hmirror, 0);
    CCstatus.ImageVflip = sensorBooleanOrDefault(s->status.vflip, 0);
    CCstatus.ImageAwb = sensorBooleanOrDefault(s->status.awb, 1);
    CCstatus.ImageAec2 = sensorBooleanOrDefault(s->status.aec2, 1);
    CCstatus.ImageAecValue = sensorValueOrDefault(s->status.aec_value, 600, 0, 1200);
    CCstatus.ImageSpecialEffect = sensorValueOrDefault(s->status.special_effect, 0, 0, 6);
    CCstatus.ImageWbMode = sensorValueOrDefault(s->status.wb_mode, 0, 0, 4);
    CCstatus.ImageAeLevel = (CCstatus.CamSensor_id == OV2640_PID)
        ? sensorValueOrDefault(s->status.ae_level, 0, -2, 2)
        : sensorValueOrDefault(s->status.ae_level, 0, -5, 5);

    CCstatus.ImageDcw = sensorBooleanOrDefault(s->status.dcw, 1);
    CCstatus.ImageBpc = sensorBooleanOrDefault(s->status.bpc, 1);
    CCstatus.ImageWpc = sensorBooleanOrDefault(s->status.wpc, 1);
    CCstatus.ImageAwbGain = sensorBooleanOrDefault(s->status.awb_gain, 1);
    CCstatus.ImageAgcGain = sensorValueOrDefault(s->status.agc_gain, 8, 0, 30);
    CCstatus.ImageRawGma = sensorBooleanOrDefault(s->status.raw_gma, 1);
    CCstatus.ImageLenc = sensorBooleanOrDefault(s->status.lenc, 1);
    CCstatus.ImageSharpness = (CCstatus.CamSensor_id == OV2640_PID)
        ? sensorValueOrDefault(s->status.sharpness, 0, -2, 2)
        : sensorValueOrDefault(s->status.sharpness, 0, -3, 3);
    CCstatus.ImageDenoiseLevel = (CCstatus.CamSensor_id == OV2640_PID)
        ? 0 : sensorValueOrDefault(s->status.denoise, 0, 0, 8);

    CCstatus.ImageLedIntensity = min(8191, max(0, LedIntensity));
    SetImageWidthHeightFromResolution(CCstatus);
    return ESP_OK;
}

// on the OV5640, gainceiling must be set with the real value (x2>>>gainceilingLevel = 2, .... x128>>>gainceilingLevel = 128)
int CCamera::SetCamGainceiling(sensor_t *s, gainceiling_t gainceilingLevel)
{
    ScopedCameraLock cameraLock(m_cameraMutex);
    if (!cameraLock.acquired())
    {
        ESP_LOGW(TAG, "SetCamGainceiling: camera mutex unavailable");
        return ESP_ERR_TIMEOUT;
    }
    if (s == NULL) return ESP_ERR_INVALID_ARG;

    if (s->id.PID == OV2640_PID)
    {
        return s->set_gainceiling(s, gainceilingLevel);
    }

    const int level = (1 << (static_cast<int>(gainceilingLevel) + 1));
    const int highResult = s->set_reg(s, 0x3A18, 0xFF, (level >> 8) & 3);
    const int lowResult = s->set_reg(s, 0x3A19, 0xFF, level & 0xFF);
    const int result = highResult != 0 ? highResult : lowResult;
    if (result == 0) s->status.gainceiling = gainceilingLevel;
    return result;
}

void CCamera::SetCamSharpness(bool autoSharpnessEnabled, int sharpnessLevel)
{
    ScopedCameraLock cameraLock(m_cameraMutex);
    if (!cameraLock.acquired())
    {
        ESP_LOGW(TAG, "SetCamSharpness: camera mutex unavailable");
        return;
    }

    sensor_t *s = esp_camera_sensor_get();

    if (s != NULL)
    {
        if (s->id.PID == OV2640_PID)
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
    ScopedCameraLock cameraLock(m_cameraMutex);
    if (!cameraLock.acquired())
    {
        ESP_LOGW(TAG, "SetCamSpecialEffect: camera mutex unavailable");
        return;
    }

    if (s == NULL) return;

    if (s->id.PID == OV2640_PID)
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
    ScopedCameraLock cameraLock(m_cameraMutex);
    if (!cameraLock.acquired())
    {
        ESP_LOGW(TAG, "SetCamContrastBrightness: camera mutex unavailable");
        return;
    }

    if (s == NULL) return;

    if (s->id.PID == OV2640_PID)
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
void CCamera::SanitizeZoomParams(const CameraSettings &settings, int imageSize,
                                   int frameSizeX, int frameSizeY, int &imageWidth, int &imageHeight,
                                   int &zoomOffsetX, int &zoomOffsetY)
{
    imageWidth = min(frameSizeX, max(1, settings.ImageWidth + imageSize * 32));
    imageHeight = min(frameSizeY, max(1, settings.ImageHeight + imageSize * 24));

    const int availableX = max(0, frameSizeX - imageWidth);
    const int availableY = max(0, frameSizeY - imageHeight);
    const int halfX = availableX / 2;
    const int halfY = availableY / 2;

    zoomOffsetX = halfX + min(halfX, max(-halfX, zoomOffsetX));
    zoomOffsetY = halfY + min(halfY, max(-halfY, zoomOffsetY));
}

esp_err_t CCamera::SetZoomSize(const CameraSettings &settings)
{
    ScopedCameraLock cameraLock(m_cameraMutex);
    if (!cameraLock.acquired()) return ESP_ERR_TIMEOUT;

    sensor_t *s = esp_camera_sensor_get();
    if (s == NULL) return ESP_FAIL;

    if (!settings.ImageZoomEnabled)
    {
        return s->set_framesize(s, settings.ImageFrameSize) == 0 ? ESP_OK : ESP_FAIL;
    }

    int frameSizeX = 0;
    int frameSizeY = 0;
    int maxZoom = 0;
    switch (s->id.PID)
    {
    case OV5640_PID:
        frameSizeX = 2592;
        frameSizeY = 1944;
        maxZoom = 59;
        break;
    case OV3660_PID:
        frameSizeX = 2048;
        frameSizeY = 1536;
        maxZoom = 43;
        break;
    case OV2640_PID:
        frameSizeX = 1600;
        frameSizeY = 1200;
        maxZoom = 29;
        break;
    default:
        ESP_LOGE(TAG, "SetZoomSize: unsupported sensor PID 0x%04x", s->id.PID);
        return ESP_ERR_NOT_SUPPORTED;
    }

    const int zoomSize = min(maxZoom, max(0, settings.ImageZoomSize));
    const int zoomDelta = maxZoom - zoomSize;
    int imageWidth = settings.ImageWidth;
    int imageHeight = settings.ImageHeight;
    int offsetX = settings.ImageZoomOffsetX;
    int offsetY = settings.ImageZoomOffsetY;

    SanitizeZoomParams(settings, zoomDelta, frameSizeX, frameSizeY,
                       imageWidth, imageHeight, offsetX, offsetY);
    SetCamWindow(s, frameSizeX, frameSizeY, offsetX, offsetY,
                 imageWidth, imageHeight, settings.ImageWidth, settings.ImageHeight,
                 settings.ImageVflip);
    return ESP_OK;
}

esp_err_t CCamera::SetQualityZoomSize(CameraSettings &settings)
{
    ScopedCameraLock cameraLock(m_cameraMutex);
    if (!cameraLock.acquired()) return ESP_ERR_TIMEOUT;

    sensor_t *s = esp_camera_sensor_get();
    if (s == NULL) return ESP_FAIL;

    SetImageWidthHeightFromResolution(settings);
    int quality = settings.ImageQuality;
    if (s->id.PID != OV2640_PID)
    {
        quality = min(63, max(8, quality));
    }
    settings.ImageQuality = quality;

    const int result = s->set_quality(s, quality);
    if (result != 0) return ESP_FAIL;
    return SetZoomSize(settings);
}

void CCamera::SetCamWindow(sensor_t *s, int frameSizeX, int frameSizeY, int xOffset, int yOffset, int xTotal, int yTotal, int xOutput, int yOutput, int imageVflip)
{
    ScopedCameraLock cameraLock(m_cameraMutex);
    if (!cameraLock.acquired())
    {
        ESP_LOGW(TAG, "SetCamWindow: camera mutex unavailable");
        return;
    }

    if (s == NULL) return;

    if (s->id.PID == OV2640_PID)
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
    ScopedCameraLock cameraLock(m_cameraMutex);
    if (!cameraLock.acquired())
    {
        ESP_LOGW(TAG, "CaptureToBasisImage: camera mutex unavailable");
        return ESP_ERR_TIMEOUT;
    }
    const bool previewCapture = CFstatus.previewCapturePending;
    const esp_err_t restoreResult = RestoreCommittedSettingsIfNeeded(previewCapture);
    if (restoreResult != ESP_OK) return restoreResult;

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
        if (delay > 0) LightOnOff(false);
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
    if (copied && previewCapture) CFstatus.previewCapturePending = false;

#ifdef DEBUG_DETAIL_ON
    LogFile.WriteHeapInfo("CaptureToBasisImage - Done");
#endif
    return copied ? ESP_OK : ESP_FAIL;
}

esp_err_t CCamera::CaptureToFile(std::string nm, int delay)
{
    ScopedCameraLock cameraLock(m_cameraMutex);
    if (!cameraLock.acquired())
    {
        ESP_LOGW(TAG, "CaptureToFile: camera mutex unavailable");
        return ESP_ERR_TIMEOUT;
    }
    const bool previewCapture = CFstatus.previewCapturePending;
    const esp_err_t restoreResult = RestoreCommittedSettingsIfNeeded(previewCapture);
    if (restoreResult != ESP_OK) return restoreResult;

    const CameraSettings &captureSettings = previewCapture
        ? static_cast<const CameraSettings &>(CFstatus)
        : static_cast<const CameraSettings &>(CCstatus);
    string ftype;

    LEDOnOff(true); // Status-LED on

    if (delay > 0)
    {
        LightOnOff(true); // Flash-LED on
        vTaskDelay(pdMS_TO_TICKS(delay));
    }

    camera_fb_t *warmup = esp_camera_fb_get();
    if (warmup != NULL) esp_camera_fb_return(warmup);
    camera_fb_t *fb = esp_camera_fb_get();

    if (!fb)
    {
        LEDOnOff(false);   // Status-LED off
        if (delay > 0) LightOnOff(false); // Flash-LED off
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
            captureOkay = frame2jpg(fb, captureSettings.ImageQuality, &buf, &buf_len) &&
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

    if (captureOkay && previewCapture) CFstatus.previewCapturePending = false;
    return captureOkay ? ESP_OK : ESP_FAIL;
}

esp_err_t CCamera::CaptureToHTTP(httpd_req_t *req, int delay)
{
    ScopedCameraLock cameraLock(m_cameraMutex);
    if (!cameraLock.acquired())
    {
        ESP_LOGW(TAG, "CaptureToHTTP: camera mutex unavailable");
        httpd_resp_set_status(req, "503 Service Unavailable");
        httpd_resp_send(req, "Camera is busy", HTTPD_RESP_USE_STRLEN);
        return ESP_ERR_TIMEOUT;
    }
    const bool previewCapture = CFstatus.previewCapturePending;
    const esp_err_t restoreResult = RestoreCommittedSettingsIfNeeded(previewCapture);
    if (restoreResult != ESP_OK) return restoreResult;

    esp_err_t res = ESP_OK;
    size_t fb_len = 0;
    int64_t fr_start = esp_timer_get_time();

    LEDOnOff(true); // Status-LED on

    if (delay > 0)
    {
        LightOnOff(true); // Flash-LED on
        vTaskDelay(pdMS_TO_TICKS(delay));
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
                if (res == ESP_OK && previewCapture) CFstatus.previewCapturePending = false;
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
                if (res == ESP_OK)
                {
                    res = httpd_resp_send_chunk(req, NULL, 0);
                }
                fb_len = jchunk.len;
            }
        }
    }

    esp_camera_fb_return(fb);
    if (res == ESP_OK && previewCapture) CFstatus.previewCapturePending = false;
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
    int64_t fr_start = 0;
    char part_buf[64];

    {
        ScopedCameraLock settingsLock(m_cameraMutex);
        if (!settingsLock.acquired()) return ESP_ERR_TIMEOUT;
        const esp_err_t restoreResult = RestoreCommittedSettingsIfNeeded();
        if (restoreResult != ESP_OK) return restoreResult;
    }

    LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Live stream started");
    if (FlashlightOn)
    {
        LEDOnOff(true);
        LightOnOff(true);
    }

    res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
    if (res == ESP_OK)
    {
        res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
    }

    // Discard one initial frame so that exposure and white balance can settle.
    if (res == ESP_OK)
    {
        ScopedCameraLock warmupLock(m_cameraMutex);
        if (!warmupLock.acquired())
        {
            res = ESP_ERR_TIMEOUT;
        }
        else
        {
            camera_fb_t *warmup = esp_camera_fb_get();
            if (warmup != NULL)
            {
                esp_camera_fb_return(warmup);
            }
        }
    }

    while (res == ESP_OK)
    {
        fr_start = esp_timer_get_time();
        {
            ScopedCameraLock frameLock(m_cameraMutex);
            if (!frameLock.acquired())
            {
                res = ESP_ERR_TIMEOUT;
            }
            else
            {
                camera_fb_t *fb = esp_camera_fb_get();
                if (fb == NULL)
                {
                    LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "CaptureToStream: Camera framebuffer not available");
                    res = ESP_FAIL;
                }
                else
                {
                    fb_len = fb->len;
                    const int headerLength = snprintf(part_buf, sizeof(part_buf), _STREAM_PART,
                                                       static_cast<unsigned int>(fb_len));
                    if (headerLength < 0 || static_cast<size_t>(headerLength) >= sizeof(part_buf))
                    {
                        res = ESP_FAIL;
                    }
                    else
                    {
                        res = httpd_resp_send_chunk(req, part_buf, static_cast<size_t>(headerLength));
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
                }
            }
        }

        if (res != ESP_OK)
        {
            break;
        }

        const int64_t fr_end = esp_timer_get_time();
        ESP_LOGD(TAG, "JPG: %dKB %dms", (int)(fb_len / 1024), (int)((fr_end - fr_start) / 1000));
        const int64_t fr_delta_ms = (fr_end - fr_start) / 1000;
        if (CAM_LIVESTREAM_REFRESHRATE > fr_delta_ms)
        {
            const TickType_t xDelay = pdMS_TO_TICKS(CAM_LIVESTREAM_REFRESHRATE - fr_delta_ms);
            ESP_LOGD(TAG, "Stream: sleep for: %lu ticks", (unsigned long)xDelay);
            vTaskDelay(xDelay);
        }
    }

    if (FlashlightOn)
    {
        LEDOnOff(false);
        LightOnOff(false);
    }
    LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Live stream stopped");
    return res;
}

void CCamera::LightOnOff(bool status)
{
    ScopedCameraLock cameraLock(m_cameraMutex);
    if (!cameraLock.acquired())
    {
        ESP_LOGW(TAG, "LightOnOff: camera mutex unavailable");
        return;
    }

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

void CCamera::SetImageWidthHeightFromResolution(CameraSettings &settings)
{
    switch (settings.ImageFrameSize)
    {
    case FRAMESIZE_QVGA:   settings.ImageWidth = 320;  settings.ImageHeight = 240;  break;
    case FRAMESIZE_VGA:    settings.ImageWidth = 640;  settings.ImageHeight = 480;  break;
    case FRAMESIZE_SVGA:   settings.ImageWidth = 800;  settings.ImageHeight = 600;  break;
    case FRAMESIZE_XGA:    settings.ImageWidth = 1024; settings.ImageHeight = 768;  break;
    case FRAMESIZE_HD:     settings.ImageWidth = 1280; settings.ImageHeight = 720;  break;
    case FRAMESIZE_SXGA:   settings.ImageWidth = 1280; settings.ImageHeight = 1024; break;
    case FRAMESIZE_UXGA:   settings.ImageWidth = 1600; settings.ImageHeight = 1200; break;
    case FRAMESIZE_QXGA:   settings.ImageWidth = 2048; settings.ImageHeight = 1536; break;
    case FRAMESIZE_WQXGA:  settings.ImageWidth = 2560; settings.ImageHeight = 1600; break;
    case FRAMESIZE_QSXGA: settings.ImageWidth = 2560; settings.ImageHeight = 1920; break;
    default:
        ESP_LOGW(TAG, "Unsupported frame size %d; falling back to VGA", static_cast<int>(settings.ImageFrameSize));
        settings.ImageFrameSize = FRAMESIZE_VGA;
        settings.ImageWidth = 640;
        settings.ImageHeight = 480;
        break;
    }
}

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
