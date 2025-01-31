#pragma once

#ifndef CLASSCONTROLLCAMERA_H
#define CLASSCONTROLLCAMERA_H

#include <string>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"

#include <esp_http_server.h>

#include "esp_camera.h"

#include "CImageBasis.h"

#include "camera_config.h"
#include "../../include/defines.h"

typedef struct
{
    cam_config_t CamConfig;

    uint16_t CameraFocusLevel = 0; // temporary storage for the latest focus level

    bool CameraSettingsChanged = false;
    bool CameraTempImage = false;

    bool CameraInitSuccessful = false;
    bool CameraAFInitSuccessful = false;
    bool CameraDeepSleepEnable = false;

    bool DemoMode = false;
    bool SaveAllFiles = false;
} camera_controll_config_temp_t;
extern camera_controll_config_temp_t CCstatus;

class CCamera
{
protected:
    void ledc_init(void);
    bool loadNextDemoImage(camera_fb_t *fb);
    long GetFileSize(std::string filename);

    int CheckCamSettingsChanged(void);
    int SetCamDeepSleep(bool enable);

    int SetCamWindow(sensor_t *sensor, cam_config_t *camConfig, int frameSizeX, int frameSizeY, int xOffset, int yOffset, int xTotal, int yTotal);
    void SetImageWidthHeightFromResolution(cam_config_t *camConfig);
    void SanitizeZoomParams(cam_config_t *camConfig, int imageSize, int frameSizeX, int frameSizeY, int &imageWidth, int &imageHeight, int &zoomOffsetX, int &zoomOffsetY);

public:
    int LedIntensity = 4096;

    CCamera(void);
    esp_err_t InitCam(void);

    void LightOnOff(bool status);
    void LEDOnOff(bool status);

    esp_err_t setCCstatusToCFstatus(void);
    esp_err_t setCFstatusToCCstatus(void);

    esp_err_t setSensorConfig(cam_config_t *camConfig);
    esp_err_t getSensorConfig(cam_config_t *camConfig);

    int SetCamGainceiling(sensor_t *sensor, cam_config_t *camConfig);
    int SetCamSharpness(sensor_t *sensor, cam_config_t *camConfig);
    int SetCamSpecialEffect(sensor_t *sensor, cam_config_t *camConfig);
    int SetCamContrastBrightness(sensor_t *sensor, cam_config_t *camConfig);

    int SetQualityZoomSize(cam_config_t *camConfig);
    int SetZoomSize(sensor_t *sensor, cam_config_t *camConfig);

    int SetLEDIntensity(int _intrel);
    bool testCamera(void);
    bool getCameraInitSuccessful(void);
    void useDemoMode(void);

    framesize_t TextToFramesize(const char *text);

    esp_err_t CaptureToHTTP(httpd_req_t *req, int delay = 0);
    esp_err_t CaptureToStream(httpd_req_t *req, bool FlashlightOn);

    esp_err_t CaptureToFile(std::string filename, int delay = 0);
    esp_err_t CaptureToBasisImage(CImageBasis *_Image, int delay = 0);
};

extern CCamera Camera;
#endif
