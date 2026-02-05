#include <iostream>
#include <string>
#include <vector>
#include <regex>

#include "ClassFlowTakeImage.h"
#include "Helper.h"
#include "ClassLogFile.h"

#include "CImageBasis.h"
#include "ClassControllCamera.h"
#include "MainFlowControl.h"

#include "esp_wifi.h"
#include "esp_log.h"
#include "../../include/defines.h"
#include "psram.h"

#include <time.h>

// #define DEBUG_DETAIL_ON
// #define WIFITURNOFF

static const char *TAG = "TAKEIMAGE";

esp_err_t ClassFlowTakeImage::camera_capture(void)
{
    string nm = namerawimage;
    Camera.CaptureToFile(nm);
    time(&TimeImageTaken);
    localtime(&TimeImageTaken);

    return ESP_OK;
}

void ClassFlowTakeImage::takePictureWithFlash(int flash_duration)
{
    // in case the image is flipped, it must be reset here //
    rawImage->width = CCstatus.ImageWidth;
    rawImage->height = CCstatus.ImageHeight;

    if (Camera.CamTempImage)
    {
        rawImage->width = CFstatus.ImageWidth;
        rawImage->height = CFstatus.ImageHeight;
    }

    ESP_LOGD(TAG, "flash_duration: %d", flash_duration);

    Camera.CaptureToBasisImage(rawImage, flash_duration);

    time(&TimeImageTaken);
    localtime(&TimeImageTaken);

    if (Camera.SaveAllFiles)
    {
        rawImage->SaveToFile(namerawimage);
    }
}

void ClassFlowTakeImage::SetInitialParameter(void)
{
    TimeImageTaken = 0;
    rawImage = NULL;
    disabled = false;
    namerawimage = "/sdcard/img_tmp/raw.jpg";
}

// auslesen der Kameraeinstellungen aus der config.ini
// wird beim Start aufgerufen
bool ClassFlowTakeImage::ReadParameter(FILE *pfile, string &aktparamgraph)
{
    Camera.get_sensor_controll_config(&CCstatus); // Kamera >>> CCstatus

    aktparamgraph = trim(aktparamgraph);
    if (aktparamgraph.size() == 0)
    {
        if (!GetNextParagraph(pfile, aktparamgraph))
        {
            return false;
        }
    }

    if (aktparamgraph.compare("[TakeImage]") != 0)
    {
        // Paragraph does not fit TakeImage
        return false;
    }

    std::vector<string> splitted;

    while (getNextLine(pfile, &aktparamgraph) && !isNewParagraph(aktparamgraph))
    {
        splitted = ZerlegeZeile(aktparamgraph);

        if (splitted.size() > 1)
        {
            std::string _parameter = toUpper(splitted[0]);
            std::string _value = splitted[1];

            if (_parameter == "RAWIMAGESLOCATION")
            {
                imagesLocation = "/sdcard" + _value;
                isLogImage = true;
            }

            else if (_parameter == "RAWIMAGESRETENTION")
            {
                if (isStringNumeric(_value))
                {
                    if (std::stod(_value) >= 0)
                    {
                        imagesRetention = std::stod(_value);
                    }
                }
            }

            else if (_parameter == "SAVEALLFILES")
            {
                Camera.SaveAllFiles = alphanumericToBoolean(_value);
            }

            else if (_parameter == "WAITBEFORETAKINGPICTURE")
            {
                if (isStringNumeric(_value))
                {
                    int _WaitBeforePicture = std::stoi(_value);
                    if (_WaitBeforePicture >= 0)
                    {
                        CCstatus.WaitBeforePicture = _WaitBeforePicture;
                    }
                    else
                    {
                        CCstatus.WaitBeforePicture = 2;
                    }
                }
            }

            else if (_parameter == "CAMXCLKFREQMHZ")
            {
                if (isStringNumeric(_value))
                {
                    int _CamXclkFreqMhz = std::stoi(_value);
                    CCstatus.CamXclkFreqMhz = clipInt(_CamXclkFreqMhz, 20, 1);
                }
            }

            else if (_parameter == "CAMGAINCEILING")
            {
                if (isStringNumeric(_value))
                {
                    int _ImageGainceiling = std::stoi(_value);
                    switch (_ImageGainceiling)
                    {
                    case 1:
                        CCstatus.ImageGainceiling = GAINCEILING_4X;
                        break;
                    case 2:
                        CCstatus.ImageGainceiling = GAINCEILING_8X;
                        break;
                    case 3:
                        CCstatus.ImageGainceiling = GAINCEILING_16X;
                        break;
                    case 4:
                        CCstatus.ImageGainceiling = GAINCEILING_32X;
                        break;
                    case 5:
                        CCstatus.ImageGainceiling = GAINCEILING_64X;
                        break;
                    case 6:
                        CCstatus.ImageGainceiling = GAINCEILING_128X;
                        break;
                    default:
                        CCstatus.ImageGainceiling = GAINCEILING_2X;
                    }
                }
                else
                {
                    std::string _ImageGainceiling = toUpper(_value);
                    if (_ImageGainceiling == "X4")
                    {
                        CCstatus.ImageGainceiling = GAINCEILING_4X;
                    }
                    else if (_ImageGainceiling == "X8")
                    {
                        CCstatus.ImageGainceiling = GAINCEILING_8X;
                    }
                    else if (_ImageGainceiling == "X16")
                    {
                        CCstatus.ImageGainceiling = GAINCEILING_16X;
                    }
                    else if (_ImageGainceiling == "X32")
                    {
                        CCstatus.ImageGainceiling = GAINCEILING_32X;
                    }
                    else if (_ImageGainceiling == "X64")
                    {
                        CCstatus.ImageGainceiling = GAINCEILING_64X;
                    }
                    else if (_ImageGainceiling == "X128")
                    {
                        CCstatus.ImageGainceiling = GAINCEILING_128X;
                    }
                    else
                    {
                        CCstatus.ImageGainceiling = GAINCEILING_2X;
                    }
                }
            }

            else if (_parameter == "CAMQUALITY")
            {
                if (isStringNumeric(_value))
                {
                    int _ImageQuality = std::stoi(_value);
                    CCstatus.ImageQuality = clipInt(_ImageQuality, 63, 6);
                }
            }

            else if (_parameter == "CAMBRIGHTNESS")
            {
                if (isStringNumeric(_value))
                {
                    int _ImageBrightness = std::stoi(_value);
                    CCstatus.ImageBrightness = clipInt(_ImageBrightness, 2, -2);
                }
            }

            else if (_parameter == "CAMCONTRAST")
            {
                if (isStringNumeric(_value))
                {
                    int _ImageContrast = std::stoi(_value);
                    CCstatus.ImageContrast = clipInt(_ImageContrast, 2, -2);
                }
            }

            else if (_parameter == "CAMSATURATION")
            {
                if (isStringNumeric(_value))
                {
                    int _ImageSaturation = std::stoi(_value);
                    CCstatus.ImageSaturation = clipInt(_ImageSaturation, 2, -2);
                }
            }

            else if (_parameter == "CAMSHARPNESS")
            {
                if (isStringNumeric(_value))
                {
                    int _ImageSharpness = std::stoi(_value);
                    if (Camera.CamSensorId == OV2640_PID)
                    {
                        CCstatus.ImageSharpness = clipInt(_ImageSharpness, 2, -2);
                    }
                    else
                    {
                        CCstatus.ImageSharpness = clipInt(_ImageSharpness, 3, -3);
                    }
                }
            }

            else if (_parameter == "CAMAUTOSHARPNESS")
            {
                CCstatus.ImageAutoSharpness = alphanumericToBoolean(_value);
            }

            else if (_parameter == "CAMSPECIALEFFECT")
            {
                if (isStringNumeric(_value))
                {
                    int _ImageSpecialEffect = std::stoi(_value);
                    CCstatus.ImageSpecialEffect = clipInt(_ImageSpecialEffect, 6, 0);
                }
                else
                {
                    std::string _ImageSpecialEffect = toUpper(_value);
                    if (_ImageSpecialEffect == "NEGATIVE")
                    {
                        CCstatus.ImageSpecialEffect = 1;
                    }
                    else if (_ImageSpecialEffect == "GRAYSCALE")
                    {
                        CCstatus.ImageSpecialEffect = 2;
                    }
                    else if (_ImageSpecialEffect == "RED")
                    {
                        CCstatus.ImageSpecialEffect = 3;
                    }
                    else if (_ImageSpecialEffect == "GREEN")
                    {
                        CCstatus.ImageSpecialEffect = 4;
                    }
                    else if (_ImageSpecialEffect == "BLUE")
                    {
                        CCstatus.ImageSpecialEffect = 5;
                    }
                    else if (_ImageSpecialEffect == "RETRO")
                    {
                        CCstatus.ImageSpecialEffect = 6;
                    }
                    else
                    {
                        CCstatus.ImageSpecialEffect = 0;
                    }
                }
            }

            else if (_parameter == "CAMWBMODE")
            {
                if (isStringNumeric(_value))
                {
                    int _ImageWbMode = std::stoi(_value);
                    CCstatus.ImageWbMode = clipInt(_ImageWbMode, 4, 0);
                }
                else
                {
                    std::string _ImageWbMode = toUpper(_value);
                    if (_ImageWbMode == "SUNNY")
                    {
                        CCstatus.ImageWbMode = 1;
                    }
                    else if (_ImageWbMode == "CLOUDY")
                    {
                        CCstatus.ImageWbMode = 2;
                    }
                    else if (_ImageWbMode == "OFFICE")
                    {
                        CCstatus.ImageWbMode = 3;
                    }
                    else if (_ImageWbMode == "HOME")
                    {
                        CCstatus.ImageWbMode = 4;
                    }
                    else
                    {
                        CCstatus.ImageWbMode = 0;
                    }
                }
            }

            else if (_parameter == "CAMAWB")
            {
                CCstatus.ImageAwb = alphanumericToInt(_value);
            }

            else if (_parameter == "CAMAWBGAIN")
            {
                CCstatus.ImageAwbGain = alphanumericToInt(_value);
            }

            else if (_parameter == "CAMAEC")
            {
                CCstatus.ImageAec = alphanumericToInt(_value);
            }

            else if (_parameter == "CAMAEC2")
            {
                CCstatus.ImageAec2 = alphanumericToInt(_value);
            }

            else if (_parameter == "CAMAELEVEL")
            {
                if (isStringNumeric(_value))
                {
                    int _ImageAeLevel = std::stoi(_value);
                    if (Camera.CamSensorId == OV2640_PID)
                    {
                        CCstatus.ImageAeLevel = clipInt(_ImageAeLevel, 2, -2);
                    }
                    else
                    {
                        CCstatus.ImageAeLevel = clipInt(_ImageAeLevel, 5, -5);
                    }
                }
            }

            else if (_parameter == "CAMAECVALUE")
            {
                if (isStringNumeric(_value))
                {
                    int _ImageAecValue = std::stoi(_value);
                    CCstatus.ImageAecValue = clipInt(_ImageAecValue, 1200, 0);
                }
            }

            else if (_parameter == "CAMAGC")
            {
                CCstatus.ImageAgc = alphanumericToInt(_value);
            }

            else if (_parameter == "CAMAGCGAIN")
            {
                if (isStringNumeric(_value))
                {
                    int _ImageAgcGain = std::stoi(_value);
                    CCstatus.ImageAgcGain = clipInt(_ImageAgcGain, 30, 0);
                }
            }

            else if (_parameter == "CAMBPC")
            {
                CCstatus.ImageBpc = alphanumericToInt(_value);
            }

            else if (_parameter == "CAMWPC")
            {
                CCstatus.ImageWpc = alphanumericToInt(_value);
            }

            else if (_parameter == "CAMRAWGMA")
            {
                CCstatus.ImageRawGma = alphanumericToInt(_value);
            }

            else if (_parameter == "CAMLENC")
            {
                CCstatus.ImageLenc = alphanumericToInt(_value);
            }

            else if (_parameter == "CAMHMIRROR")
            {
                CCstatus.ImageHmirror = alphanumericToInt(_value);
            }

            else if (_parameter == "CAMVFLIP")
            {
                CCstatus.ImageVflip = alphanumericToInt(_value);
            }

            else if (_parameter == "CAMDCW")
            {
                CCstatus.ImageDcw = alphanumericToInt(_value);
            }

            else if (_parameter == "CAMDENOISE")
            {
                if (isStringNumeric(_value))
                {
                    int _ImageDenoiseLevel = std::stoi(_value);
                    if (Camera.CamSensorId == OV2640_PID)
                    {
                        CCstatus.ImageDenoiseLevel = 0;
                    }
                    else
                    {
                        CCstatus.ImageDenoiseLevel = clipInt(_ImageDenoiseLevel, 8, 0);
                    }
                }
            }

            else if (_parameter == "CAMZOOM")
            {
                CCstatus.ImageZoomEnabled = alphanumericToBoolean(_value);
            }

            else if (_parameter == "CAMZOOMOFFSETX")
            {
                if (isStringNumeric(_value))
                {
                    int _ImageZoomOffsetX = std::stoi(_value);
                    if (Camera.CamSensorId == OV2640_PID)
                    {
                        CCstatus.ImageZoomOffsetX = clipInt(_ImageZoomOffsetX, 480, -480);
                    }
                    else if (Camera.CamSensorId == OV3660_PID)
                    {
                        CCstatus.ImageZoomOffsetX = clipInt(_ImageZoomOffsetX, 704, -704);
                    }
                    else if (Camera.CamSensorId == OV5640_PID)
                    {
                        CCstatus.ImageZoomOffsetX = clipInt(_ImageZoomOffsetX, 960, -960);
                    }
                }
            }

            else if (_parameter == "CAMZOOMOFFSETY")
            {
                if (isStringNumeric(_value))
                {
                    int _ImageZoomOffsetY = std::stoi(_value);
                    if (Camera.CamSensorId == OV2640_PID)
                    {
                        CCstatus.ImageZoomOffsetY = clipInt(_ImageZoomOffsetY, 360, -360);
                    }
                    else if (Camera.CamSensorId == OV3660_PID)
                    {
                        CCstatus.ImageZoomOffsetY = clipInt(_ImageZoomOffsetY, 528, -528);
                    }
                    else if (Camera.CamSensorId == OV5640_PID)
                    {
                        CCstatus.ImageZoomOffsetY = clipInt(_ImageZoomOffsetY, 720, -720);
                    }
                }
            }

            else if (_parameter == "CAMZOOMSIZE")
            {
                if (isStringNumeric(_value))
                {
                    int _ImageZoomSize = std::stoi(_value);
                    if (Camera.CamSensorId == OV2640_PID)
                    {
                        CCstatus.ImageZoomSize = clipInt(_ImageZoomSize, 29, 0);
                    }
                    else if (Camera.CamSensorId == OV3660_PID)
                    {
                        CCstatus.ImageZoomSize = clipInt(_ImageZoomSize, 43, 0);
                    }
                    else if (Camera.CamSensorId == OV5640_PID)
                    {
                        CCstatus.ImageZoomSize = clipInt(_ImageZoomSize, 59, 0);
                    }
                }
            }

            else if (_parameter == "LEDINTENSITY")
            {
                if (isStringNumeric(_value))
                {
                    int ledintensity = std::stoi(_value);
                    if (ledintensity >= 0)
                    {
                        CCstatus.ImageLedIntensity = Camera.SetLEDIntensity(ledintensity);
                    }
                }
            }

            else if (_parameter == "DEMO")
            {
                Camera.DemoMode = alphanumericToBoolean(_value);
                if (Camera.DemoMode == true)
                {
                    Camera.useDemoMode();
                }
            }
        }
    }

    Camera.set_sensor_controll_config(&CCstatus); // CCstatus >>> Kamera
    Camera.set_quality_zoom_size(&CCstatus);

    Camera.changedCameraSettings = false;
    Camera.CamTempImage = false;

    rawImage = new CImageBasis("rawImage");
    rawImage->CreateEmptyImage(CCstatus.ImageWidth, CCstatus.ImageHeight, 3);

    return true;
}

ClassFlowTakeImage::ClassFlowTakeImage(std::vector<ClassFlow *> *lfc) : ClassFlowImage(lfc, TAG)
{
    imagesLocation = "/log/source";
    imagesRetention = 5;
    SetInitialParameter();
}

string ClassFlowTakeImage::getHTMLSingleStep(string host)
{
    std::string result = "Raw Image: <br>\n<img src=\"" + host + "/img_tmp/raw.jpg\">\n";
    return result;
}

// wird bei jeder Auswertrunde aufgerufen
bool ClassFlowTakeImage::doFlow(string zwtime)
{
    if (psram_init_shared_memory_for_take_image_step())
    {
        string logPath = CreateLogFolder(zwtime);

        int flash_duration = (int)(CCstatus.WaitBeforePicture * 1000);
        if (Camera.CamTempImage)
        {
            flash_duration = (int)(CFstatus.WaitBeforePicture * 1000);
        }

        takePictureWithFlash(flash_duration);
        LogImage(logPath, "raw", NULL, NULL, zwtime, rawImage);
        RemoveOldLogs();

        psram_deinit_shared_memory_for_take_image_step();
    }
    else
    {
        return false;
    }

    return true;
}

esp_err_t ClassFlowTakeImage::SendRawJPG(httpd_req_t *req)
{
    int flash_duration = (int)(CCstatus.WaitBeforePicture * 1000);
    if (Camera.CamTempImage)
    {
        flash_duration = (int)(CFstatus.WaitBeforePicture * 1000);
    }

    time(&TimeImageTaken);
    localtime(&TimeImageTaken);

    return Camera.CaptureToHTTP(req, flash_duration);
}

ImageData *ClassFlowTakeImage::SendRawImage(void)
{
    CImageBasis *tempImageBasis = new CImageBasis("SendRawImage", rawImage);
    ImageData *tempImageData;

    int flash_duration = (int)(CCstatus.WaitBeforePicture * 1000);
    if (Camera.CamTempImage)
    {
        flash_duration = (int)(CFstatus.WaitBeforePicture * 1000);
    }

    Camera.CaptureToBasisImage(tempImageBasis, flash_duration);
    time(&TimeImageTaken);
    localtime(&TimeImageTaken);

    tempImageData = tempImageBasis->writeToMemoryAsJPG();
    delete tempImageBasis;
    return tempImageData;
}

time_t ClassFlowTakeImage::getTimeImageTaken(void)
{
    return TimeImageTaken;
}

ClassFlowTakeImage::~ClassFlowTakeImage(void)
{
    delete rawImage;
}
