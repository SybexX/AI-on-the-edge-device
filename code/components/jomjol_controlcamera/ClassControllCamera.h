#pragma once

#ifndef CLASSCONTROLLCAMERA_H
#define CLASSCONTROLLCAMERA_H

#include <string>
#include <vector>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "esp_camera.h"
#include <esp_http_server.h>
#include "CameraSettings.h"
#include "CImageBasis.h"
#include "../../include/defines.h"

struct camera_controll_config_temp_t : public CameraSettings
{
    // Hardware identity/dimensions and runtime flags are not camera settings.
    uint16_t CamSensor_id;

    bool CameraInitSuccessful;
    bool DemoMode;
    bool SaveAllFiles;
};

extern camera_controll_config_temp_t CCstatus;

class CCamera
{
protected:
    SemaphoreHandle_t m_cameraMutex;
    bool m_driverInitialized;
    std::vector<std::string> m_demoFiles;

    void ledc_init(void);
    bool loadNextDemoImage(uint8_t **data, size_t *length);
    long GetFileSize(std::string filename);
    void SetCamWindow(sensor_t *s, int frameSizeX, int frameSizeY, int xOffset, int yOffset, int xTotal, int yTotal, int xOutput, int yOutput, int imageVflip);
    void SetImageWidthHeightFromResolution(CameraSettings &settings);
    void SanitizeZoomParams(const CameraSettings &settings, int imageSize, int frameSizeX, int frameSizeY,
                            int &imageWidth, int &imageHeight, int &zoomOffsetX, int &zoomOffsetY);

public:
    int LedIntensity = 4096;

    CCamera(void);
    esp_err_t InitCam(void);

    void LightOnOff(bool status);
    void LEDOnOff(bool status);

    esp_err_t setSensorDatenFromCCstatus(void);
    esp_err_t ApplySettings(CameraSettings &settings);
    esp_err_t RestoreCommittedSettingsIfNeeded(bool preserveTemporaryCapture = false);
    esp_err_t getSensorDatenToCCstatus(void);

    int SetCamGainceiling(sensor_t *s, gainceiling_t gainceilingLevel);
    void SetCamSharpness(bool autoSharpnessEnabled, int sharpnessLevel);
    void SetCamSpecialEffect(sensor_t *s, int specialEffect);
    void SetCamContrastBrightness(sensor_t *s, int _contrast, int _brightness);

    esp_err_t CaptureToHTTP(httpd_req_t *req, int delay = 0, bool preserveTemporaryCapture = false);
    esp_err_t CaptureToStream(httpd_req_t *req, bool FlashlightOn);

    esp_err_t SetQualityZoomSize(CameraSettings &settings);
    esp_err_t SetZoomSize(const CameraSettings &settings);

    int SetLEDIntensity(int _intrel);
    bool testCamera(void);
    bool getCameraInitSuccessful(void);
    void useDemoMode(void);


    esp_err_t CaptureToFile(std::string nm, int delay = 0);
    esp_err_t CaptureToBasisImage(CImageBasis *_Image, int delay = 0);
};

extern CCamera Camera;
#endif