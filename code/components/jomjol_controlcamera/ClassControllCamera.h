#pragma once

#ifndef CLASSCONTROLLCAMERA_H
#define CLASSCONTROLLCAMERA_H

#include <string>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/event_groups.h>

#include <esp_http_server.h>

#include "esp_camera.h"
#include "CImageBasis.h"

#include "Helper.h"
#include "../../include/defines.h"

typedef struct {
    int CamXclkFreqMhz;

    framesize_t ImageFrameSize = FRAMESIZE_VGA; // 0 - 10
    int ImageGainceiling = 1;                   // Image Gain (GAINCEILING_x2, x4, x8, x16, x32, x64 or x128)

    int ImageQuality = 12;   // 0 - 63
    int ImageBrightness = 0; // -2 - 2
    int ImageContrast = 0;   // -2 - 2
    int ImageSaturation = 0; // -2 - 2
    int ImageSharpness = 0;  // -2 - 2
    bool ImageAutoSharpness = false;
    int ImageSpecialEffect = 0; // 0 - 6
    int ImageWbMode = 0;        // 0 to 4 - if awb_gain enabled (0 - Auto, 1 - Sunny, 2 - Cloudy, 3 - Office, 4 - Home)
    int ImageAwb = 1;           // Auto White Balance enable (0 or 1)
    int ImageAwbGain = 1;       // Auto White Balance Gain enable (0 or 1)
    int ImageAec = 1;           // Auto Exposure off (1 or 0)
    int ImageAec2 = 1;          // Automatic Exposure Control  (0 or 1)
    int ImageAeLevel = 0;       // Auto Exposure Level (-2 to 2)
    int ImageAecValue = 160;    // Automatic Exposure Control Value (for Manually Exposure Mode)  (0-1200)
    int ImageAgc = 1;           // Auto Gain Control off (1 or 0)
    int ImageAgcGain = 15;      // Gain for manually mode (0 - 30)
    int ImageBpc = 1;           // Black Pixel Correction
    int ImageWpc = 1;           // White Pixel Correction
    int ImageRawGma = 1;        // (1 or 0)
    int ImageLenc = 1;          // Lens Correction (1 or 0)
    int ImageHmirror = 0;       // (0 or 1) flip horizontally
    int ImageVflip = 0;         // (0 or 1) flip vertical
    int ImageDcw = 1;           // Downsize enable (1 or 0)

    int ImageDenoiseLevel = 0; // The OV2640 does not support it, OV3660 and OV5640 (0 to 8)

    int ImageWidth = 640;
    int ImageHeight = 480;
    float ImageInitialRotate = 0.0;
    bool ImageAntialiasing = true;

    int ImageLedIntensity = 50;

    bool ImageZoomEnabled = false;
    int ImageZoomOffsetX = 0;
    int ImageZoomOffsetY = 0;
    int ImageZoomSize = 0;

    bool ImageNegative = false;

    int WaitBeforePicture = 5;
} camera_controll_config_t;
extern camera_controll_config_t CameraConfigSaved;
extern camera_controll_config_t CameraConfigTemp;

class CCamera
{
  protected:
    bool loadNextDemoImage(camera_fb_t *fb);
    long GetFileSize(std::string filename);
    int SetZoomSize(sensor_t *sensor, camera_controll_config_t *camConfig);
    int SetCamWindow(sensor_t *sensor, camera_controll_config_t *camConfig, int frameSizeX, int frameSizeY, int xOffset, int yOffset, int xTotal, int yTotal);
    void SetImageWidthHeightFromResolution(camera_controll_config_t *camConfig);
    void SanitizeZoomParams(camera_controll_config_t *camConfig, int imageSize, int frameSizeX, int frameSizeY, int &imageWidth, int &imageHeight, int &zoomOffsetX, int &zoomOffsetY);

  public:
    bool DemoMode = false;
    bool SaveAllFiles = false;

    int LedIntensity = 4096;
    bool CaptureToBasisImageLed = false;
    bool CaptureToFileLed = false;
    bool CaptureToHTTPLed = false;
    bool CaptureToStreamLed = false;

    uint16_t CamSensor_id;
    bool CameraDeepSleepEnable = false;
    bool CamInitSuccessful = false;
    bool CamSettingsChanged = false;
    bool CamTempImage = false;

    CCamera(void);
    esp_err_t InitCam(void);
    void ResetCamera(void);
    void PowerResetCamera(void);

    bool testCamera(void);

    int SetLEDIntensity(int _intrel);
    bool getCameraInitSuccessful(void);

    int CheckCamSettingsChanged(void);
    int SetCamDeepSleep(bool enable);

    esp_err_t SetSensorControllConfig(camera_controll_config_t *camConfig);
    esp_err_t GetSensorControllConfig(camera_controll_config_t *camConfig);
    esp_err_t SetCameraConfigFromTo(camera_controll_config_t *camConfigFrom, camera_controll_config_t *camConfigTo);

    int SetCamSharpness(sensor_t *sensor, bool autoSharpnessEnabled, int sharpnessLevel);

    int SetQualityZoomSize(camera_controll_config_t *camConfig);

    esp_err_t CaptureToBasisImage(CImageBasis *_Image, int flash_duration = 0);
    esp_err_t CaptureToFile(std::string pathImage, int flash_duration = 0);
    esp_err_t CaptureToHTTP(httpd_req_t *req, int flash_duration = 0);
    esp_err_t CaptureToStream(httpd_req_t *req, bool flashlightOn);

    void FlashLightOnOff(bool status, int intensity);
    void StatusLEDOnOff(bool status);

    framesize_t TextToFramesize(const char *text);

    void useDemoMode(void);
};

extern CCamera Camera;

#endif
