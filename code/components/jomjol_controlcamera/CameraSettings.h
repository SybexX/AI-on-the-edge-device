#pragma once

#ifndef JOMJOL_CAMERA_SETTINGS_H
#define JOMJOL_CAMERA_SETTINGS_H

#include "esp_camera.h"

// Shared sensor/image parameters. Runtime state and temporary-flow flags live
// in their respective status structures, not in this common settings object.
struct CameraSettings
{
    framesize_t ImageFrameSize = FRAMESIZE_VGA;
    gainceiling_t ImageGainceiling;

    int ImageQuality;
    int ImageBrightness;
    int ImageContrast;
    int ImageSaturation;
    int ImageSharpness;
    bool ImageAutoSharpness;
    int ImageSpecialEffect;
    int ImageWbMode;
    int ImageAwb;
    int ImageAwbGain;
    int ImageAec;
    int ImageAec2;
    int ImageAeLevel;
    int ImageAecValue;
    int ImageAgc;
    int ImageAgcGain;
    int ImageBpc;
    int ImageWpc;
    int ImageRawGma;
    int ImageLenc;
    int ImageHmirror;
    int ImageVflip;
    int ImageDcw;
    int ImageDenoiseLevel;

    int ImageLedIntensity;

    bool ImageZoomEnabled;
    int ImageZoomOffsetX;
    int ImageZoomOffsetY;
    int ImageZoomSize;

    int WaitBeforePicture;
};

#endif // JOMJOL_CAMERA_SETTINGS_H
