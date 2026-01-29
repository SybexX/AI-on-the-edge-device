#pragma once

#ifndef CLASSCONTROLLCAMERA_H
#define CLASSCONTROLLCAMERA_H

#include <string>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"

#include "esp_camera.h"
#include <string>
#include <esp_http_server.h>
#include "CImageBasis.h"
#include "../../include/defines.h"

typedef struct
{
    int CamXclkFreqMhz = 16; // 1 to 20

    framesize_t ImageFrameSize = FRAMESIZE_VGA;      // 0 to 10
    gainceiling_t ImageGainceiling = GAINCEILING_4X; // Image gain (GAINCEILING_x2, x4, x8, x16, x32, x64 or x128)

    int ImageQuality = 12;           // 0 to 63
    int ImageBrightness = 0;         // -2 to 2
    int ImageContrast = 0;           // -2 to 2
    int ImageSaturation = 0;         // -2 to 2
    int ImageSharpness = 0;          // -2 to 2
    bool ImageAutoSharpness = false; // false - true
    int ImageSpecialEffect = 0;      // 0 to 6
    int ImageWbMode = 0;             // 0 to 4 - if awb_gain enabled (0 - Auto, 1 - Sunny, 2 - Cloudy, 3 - Office, 4 - Home)
    int ImageAwb = 1;                // white balance enable (0 or 1)
    int ImageAwbGain = 1;            // Auto White Balance enable (0 or 1)
    int ImageAec = 1;                // auto exposure off (0 or 1)
    int ImageAec2 = 1;               // automatic exposure sensor  (0 or 1)
    int ImageAeLevel = 0;            // auto exposure levels (-2 to 2)
    int ImageAecValue = 600;         // set exposure manually  (0 to 1200)
    int ImageAgc = 1;                // auto gain off (0 or 1)
    int ImageAgcGain = 8;            // set gain manually (0 to 30)
    int ImageBpc = 1;                // black pixel correction (0 or 1)
    int ImageWpc = 1;                // white pixel correction (0 or 1)
    int ImageRawGma = 1;             // raw gma (0 or 1)
    int ImageLenc = 1;               // lens correction (0 or 1)
    int ImageHmirror = 0;            // flip horizontally (0 or 1)
    int ImageVflip = 0;              // invert image (0 or 1)
    int ImageDcw = 1;                // downsize enable (0 or 1)

    int ImageDenoiseLevel = 0; // The OV2640 does not support it, OV3660 and OV5640 (0 to 8)

    int ImageWidth = 640;
    int ImageHeight = 480;

    int ImageLedIntensity = 4096;

    bool ImageZoomEnabled = false;
    int ImageZoomOffsetX = 0;
    int ImageZoomOffsetY = 0;
    int ImageZoomSize = 0;

    int WaitBeforePicture = 2;
} camera_controll_config_temp_t;

extern camera_controll_config_temp_t CCstatus;
extern camera_controll_config_temp_t CFstatus;

class CCamera
{
protected:
    sensor_t *cam_sensor;

    void ledc_init(void);
    bool loadNextDemoImage(camera_fb_t *fb);
    long GetFileSize(std::string filename);
    esp_err_t set_camera_window(camera_controll_config_temp_t *camConfig, int frameSizeX, int frameSizeY, int xOffset, int yOffset, int xTotal, int yTotal);
    void set_image_width_height_from_resolution(framesize_t resol);
    void sanitize_zoom_params(camera_controll_config_temp_t *camConfig, int imageSize, int frameSizeX, int frameSizeY, int &imageWidth, int &imageHeight, int &zoomOffsetX, int &zoomOffsetY);

public:
    uint16_t CamSensorId = OV2640_PID;

    int LedIntensity = 4096;
    bool CaptureToBasisImageLed = false;
    bool CaptureToFileLed = false;
    bool CaptureToHTTPLed = false;
    bool CaptureToStreamLed = false;

    bool DemoMode = false;
    bool SaveAllFiles = false;
    bool ImageAntialiasing = false;
    float ImageInitialRotate = 0.0;
    bool ImageInitialFlip = false;

    bool CamDeepSleepEnable = false;
    bool CameraInitSuccessful = false;
    bool changedCameraSettings = false;
    bool CamTempImage = false;

    CCamera(void);
    esp_err_t InitCam(void);

    void LightOnOff(bool status);
    void LEDOnOff(bool status);

    esp_err_t set_sensor_controll_config(camera_controll_config_temp_t *camConfig);
    esp_err_t get_sensor_controll_config(camera_controll_config_temp_t *camConfig);
    esp_err_t set_camera_config_from_to(camera_controll_config_temp_t *camConfigFrom, camera_controll_config_temp_t *camConfigTo);

    esp_err_t check_camera_settings_changed(void);
    esp_err_t set_camera_deep_sleep(bool status);

    esp_err_t set_camera_gainceiling(gainceiling_t gainceilingLevel);
    esp_err_t set_camera_sharpness(bool autoSharpnessEnabled, int sharpnessLevel);
    esp_err_t set_camera_special_effect(int specialEffect);
    esp_err_t set_camera_contrast_brightness(int _contrast, int _brightness);

    esp_err_t set_quality_zoom_size(camera_controll_config_temp_t *camConfig);
    esp_err_t set_zoom_size(camera_controll_config_temp_t *camConfig);

    int SetLEDIntensity(int _intrel);
    bool testCamera(void);
    bool getCameraInitSuccessful(void);
    void useDemoMode(void);

    framesize_t TextToFramesize(const char *text);

    esp_err_t CaptureToFile(std::string nm, int flash_duration = 0);
    esp_err_t CaptureToBasisImage(CImageBasis *_Image, int flash_duration = 0);

    esp_err_t CaptureToHTTP(httpd_req_t *req, int flash_duration = 0);
    esp_err_t CaptureToStream(httpd_req_t *req, bool FlashlightOn);
};

extern CCamera Camera;
#endif
