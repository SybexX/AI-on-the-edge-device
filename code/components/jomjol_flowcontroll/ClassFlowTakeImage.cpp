#include <iostream>
#include <string>
#include <vector>
#include <regex>
#include <time.h>

#include "ClassFlowTakeImage.h"
#include "ClassLogFile.h"

#include "CImageBasis.h"
#include "ClassControllCamera.h"
#include "MainFlowControl.h"

#include "esp_wifi.h"
#include "esp_log.h"

#include "psram.h"

#include "Helper.h"
#include "defines.h"

// #define DEBUG_DETAIL_ON

static const char *TAG = "TAKEIMAGE";

void ClassFlowTakeImage::SetInitialParameter(void)
{
    TimeImageTaken = 0;
    rawImage = NULL;
    disabled = false;
    pathRawImage = "/sdcard/img_tmp/raw.jpg";
}

// auslesen der Kameraeinstellungen aus der config.ini
// wird beim Start aufgerufen
bool ClassFlowTakeImage::ReadParameter(FILE *pfile, std::string &aktparamgraph)
{
    // ESP_LOGI(TAG, "ClassFlowTakeImage::ReadParameter: GetSensorControllConfig(&CameraConfigSaved)");
    Camera.GetSensorControllConfig(&CameraConfigSaved); // Kamera >>> CameraConfigSaved

    std::vector<std::string> splitted;
    aktparamgraph = trim_string_left_right(aktparamgraph);

    if (aktparamgraph.size() == 0)
    {
        if (!this->GetNextParagraph(pfile, aktparamgraph))
        {
            return false;
        }
    }

    if (toUpper(aktparamgraph).compare("[TAKEIMAGE]") != 0)
    {
        // Paragraph does not fit TakeImage
        return false;
    }

    int existing_parameters = 34;

    while (this->getNextLine(pfile, &aktparamgraph) && !this->isNewParagraph(aktparamgraph))
    {
        splitted = splitLine(aktparamgraph);
        std::string _param = toUpper(splitted[0]);

        if (splitted.size() > 1)
        {
            if (_param == "RAWIMAGESLOCATION")
            {
                imagesLocation = "/sdcard" + splitted[1];
                isLogImage = true;
                // existing_parameters = existing_parameters - 1;
            }

            else if (_param == "RAWIMAGESRETENTION")
            {
                if (isStringNumeric(splitted[1]))
                {
                    this->imagesRetention = std::stod(splitted[1]);
                }
                // existing_parameters = existing_parameters - 1;
            }

            else if (_param == "SAVEALLFILES")
            {
                Camera.SaveAllFiles = alphanumericToBoolean(splitted[1]);
                // existing_parameters = existing_parameters - 1;
            }

            else if (_param == "WAITBEFORETAKINGPICTURE")
            {
                if (isStringNumeric(splitted[1]))
                {
                    int _WaitBeforePicture = std::stoi(splitted[1]);
                    if (_WaitBeforePicture >= 0)
                    {
                        CameraConfigSaved.WaitBeforePicture = _WaitBeforePicture;
                    }
                }
                existing_parameters = existing_parameters - 1;
            }

            else if (_param == "CAMXCLKFREQMHZ")
            {
                if (isStringNumeric(splitted[1]))
                {
                    int _CamXclkFreqMhz = std::stoi(splitted[1]);
                    CameraConfigSaved.CamXclkFreqMhz = clipInt(_CamXclkFreqMhz, 20, 1);
                }
                existing_parameters = existing_parameters - 1;
            }

            else if (_param == "CAMGAINCEILING")
            {
                std::string _ImageGainceiling = toUpper(splitted[1]);
                // ESP_LOGI(TAG, "ImageGainceiling = %s", _ImageGainceiling.c_str());
                if (isStringNumeric(_ImageGainceiling))
                {
                    int _ImageGainceiling_ = std::stoi(_ImageGainceiling);
                    CameraConfigSaved.ImageGainceiling = clipInt(_ImageGainceiling_, 7, 0);
                }
                else
                {
                    if (_ImageGainceiling == "X2")
                    {
                        CameraConfigSaved.ImageGainceiling = 0;
                    }
                    else if (_ImageGainceiling == "X4")
                    {
                        CameraConfigSaved.ImageGainceiling = 1;
                    }
                    else if (_ImageGainceiling == "X8")
                    {
                        CameraConfigSaved.ImageGainceiling = 2;
                    }
                    else if (_ImageGainceiling == "X16")
                    {
                        CameraConfigSaved.ImageGainceiling = 3;
                    }
                    else if (_ImageGainceiling == "X32")
                    {
                        CameraConfigSaved.ImageGainceiling = 4;
                    }
                    else if (_ImageGainceiling == "X64")
                    {
                        CameraConfigSaved.ImageGainceiling = 5;
                    }
                    else if (_ImageGainceiling == "X128")
                    {
                        CameraConfigSaved.ImageGainceiling = 6;
                    }
                    else if (_ImageGainceiling == "X256")
                    {
                        CameraConfigSaved.ImageGainceiling = 7;
                    }
                }
                existing_parameters = existing_parameters - 1;
            }

            else if (_param == "CAMQUALITY")
            {
                if (isStringNumeric(splitted[1]))
                {
                    int _ImageQuality = std::stoi(splitted[1]);
                    CameraConfigSaved.ImageQuality = clipInt(_ImageQuality, 63, 6);
                }
                existing_parameters = existing_parameters - 1;
            }

            else if (_param == "CAMBRIGHTNESS")
            {
                if (isStringNumeric(splitted[1]))
                {
                    int _ImageBrightness = std::stoi(splitted[1]);
                    CameraConfigSaved.ImageBrightness = clipInt(_ImageBrightness, 2, -2);
                }
                existing_parameters = existing_parameters - 1;
            }

            else if (_param == "CAMCONTRAST")
            {
                if (isStringNumeric(splitted[1]))
                {
                    int _ImageContrast = std::stoi(splitted[1]);
                    CameraConfigSaved.ImageContrast = clipInt(_ImageContrast, 2, -2);
                }
                existing_parameters = existing_parameters - 1;
            }

            else if (_param == "CAMSATURATION")
            {
                if (isStringNumeric(splitted[1]))
                {
                    int _ImageSaturation = std::stoi(splitted[1]);
                    CameraConfigSaved.ImageSaturation = clipInt(_ImageSaturation, 2, -2);
                }
                existing_parameters = existing_parameters - 1;
            }

            else if (_param == "CAMSHARPNESS")
            {
                if (isStringNumeric(splitted[1]))
                {
                    int _ImageSharpness = std::stoi(splitted[1]);
                    if (Camera.CamSensor_id == OV2640_PID)
                    {
                        CameraConfigSaved.ImageSharpness = clipInt(_ImageSharpness, 2, -2);
                    }
#if CONFIG_OV3660_SUPPORT || CONFIG_OV5640_SUPPORT
                    else if ((Camera.CamSensor_id == OV3660_PID) || (Camera.CamSensor_id == OV5640_PID))
                    {
                        CameraConfigSaved.ImageSharpness = clipInt(_ImageSharpness, 3, -3);
                    }
#endif
                }
                existing_parameters = existing_parameters - 1;
            }

            else if (_param == "CAMAUTOSHARPNESS")
            {
                CameraConfigSaved.ImageAutoSharpness = alphanumericToBoolean(splitted[1]);
                existing_parameters = existing_parameters - 1;
            }

            else if (_param == "CAMSPECIALEFFECT")
            {
                std::string _ImageSpecialEffect = toUpper(splitted[1]);

                if (isStringNumeric(_ImageSpecialEffect))
                {
                    int _ImageSpecialEffect_ = std::stoi(_ImageSpecialEffect);
                    CameraConfigSaved.ImageSpecialEffect = clipInt(_ImageSpecialEffect_, 6, 0);
                }
                else
                {
                    if (_ImageSpecialEffect == "NO_EFFECT")
                    {
                        CameraConfigSaved.ImageSpecialEffect = 0;
                    }
                    else if (_ImageSpecialEffect == "NEGATIVE")
                    {
                        CameraConfigSaved.ImageSpecialEffect = 1;
                    }
                    else if (_ImageSpecialEffect == "GRAYSCALE")
                    {
                        CameraConfigSaved.ImageSpecialEffect = 2;
                    }
                    else if (_ImageSpecialEffect == "RED")
                    {
                        CameraConfigSaved.ImageSpecialEffect = 3;
                    }
                    else if (_ImageSpecialEffect == "GREEN")
                    {
                        CameraConfigSaved.ImageSpecialEffect = 4;
                    }
                    else if (_ImageSpecialEffect == "BLUE")
                    {
                        CameraConfigSaved.ImageSpecialEffect = 5;
                    }
                    else if (_ImageSpecialEffect == "RETRO")
                    {
                        CameraConfigSaved.ImageSpecialEffect = 6;
                    }
                }
                existing_parameters = existing_parameters - 1;
            }

            else if (_param == "CAMWBMODE")
            {
                std::string _ImageWbMode = toUpper(splitted[1]);

                if (isStringNumeric(_ImageWbMode))
                {
                    int _ImageWbMode_ = std::stoi(_ImageWbMode);
                    CameraConfigSaved.ImageWbMode = clipInt(_ImageWbMode_, 4, 0);
                }
                else
                {
                    if (_ImageWbMode == "AUTO")
                    {
                        CameraConfigSaved.ImageWbMode = 0;
                    }
                    else if (_ImageWbMode == "SUNNY")
                    {
                        CameraConfigSaved.ImageWbMode = 1;
                    }
                    else if (_ImageWbMode == "CLOUDY")
                    {
                        CameraConfigSaved.ImageWbMode = 2;
                    }
                    else if (_ImageWbMode == "OFFICE")
                    {
                        CameraConfigSaved.ImageWbMode = 3;
                    }
                    else if (_ImageWbMode == "HOME")
                    {
                        CameraConfigSaved.ImageWbMode = 4;
                    }
                }
                existing_parameters = existing_parameters - 1;
            }

            else if (_param == "CAMAWB")
            {
                CameraConfigSaved.ImageAwb = alphanumericToInteger(splitted[1]);
                existing_parameters = existing_parameters - 1;
            }

            else if (_param == "CAMAWBGAIN")
            {
                CameraConfigSaved.ImageAwbGain = alphanumericToInteger(splitted[1]);
                existing_parameters = existing_parameters - 1;
            }

            else if (_param == "CAMAEC")
            {
                CameraConfigSaved.ImageAec = alphanumericToInteger(splitted[1]);
                existing_parameters = existing_parameters - 1;
            }

            else if (_param == "CAMAEC2")
            {
                CameraConfigSaved.ImageAec2 = alphanumericToInteger(splitted[1]);
                existing_parameters = existing_parameters - 1;
            }

            else if (_param == "CAMAELEVEL")
            {
                if (isStringNumeric(splitted[1]))
                {
                    int _ImageAeLevel = std::stoi(splitted[1]);
                    if (Camera.CamSensor_id == OV2640_PID)
                    {
                        CameraConfigSaved.ImageAeLevel = clipInt(_ImageAeLevel, 2, -2);
                    }
#if CONFIG_OV3660_SUPPORT || CONFIG_OV5640_SUPPORT
                    else if ((Camera.CamSensor_id == OV3660_PID) || (Camera.CamSensor_id == OV5640_PID))
                    {
                        CameraConfigSaved.ImageAeLevel = clipInt(_ImageAeLevel, 5, -5);
                    }
#endif
                }
                existing_parameters = existing_parameters - 1;
            }

            else if (_param == "CAMAECVALUE")
            {
                if (isStringNumeric(splitted[1]))
                {
                    int _ImageAecValue = std::stoi(splitted[1]);
                    CameraConfigSaved.ImageAecValue = clipInt(_ImageAecValue, 1200, 0);
                }
                existing_parameters = existing_parameters - 1;
            }

            else if (_param == "CAMAGC")
            {
                CameraConfigSaved.ImageAgc = alphanumericToInteger(splitted[1]);
                existing_parameters = existing_parameters - 1;
            }

            else if (_param == "CAMAGCGAIN")
            {
                if (isStringNumeric(splitted[1]))
                {
                    int _ImageAgcGain = std::stoi(splitted[1]);
                    CameraConfigSaved.ImageAgcGain = clipInt(_ImageAgcGain, 30, 0);
                }
                existing_parameters = existing_parameters - 1;
            }

            else if (_param == "CAMBPC")
            {
                CameraConfigSaved.ImageBpc = alphanumericToInteger(splitted[1]);
                existing_parameters = existing_parameters - 1;
            }

            else if (_param == "CAMWPC")
            {
                CameraConfigSaved.ImageWpc = alphanumericToInteger(splitted[1]);
                existing_parameters = existing_parameters - 1;
            }

            else if (_param == "CAMRAWGMA")
            {
                CameraConfigSaved.ImageRawGma = alphanumericToInteger(splitted[1]);
                existing_parameters = existing_parameters - 1;
            }

            else if (_param == "CAMLENC")
            {
                CameraConfigSaved.ImageLenc = alphanumericToInteger(splitted[1]);
                existing_parameters = existing_parameters - 1;
            }

            else if (_param == "CAMHMIRROR")
            {
                CameraConfigSaved.ImageHmirror = alphanumericToInteger(splitted[1]);
                existing_parameters = existing_parameters - 1;
            }

            else if (_param == "CAMVFLIP")
            {
                CameraConfigSaved.ImageVflip = alphanumericToInteger(splitted[1]);
                existing_parameters = existing_parameters - 1;
            }

            else if (_param == "CAMDCW")
            {
                CameraConfigSaved.ImageDcw = alphanumericToInteger(splitted[1]);
                existing_parameters = existing_parameters - 1;
            }

            else if (_param == "CAMDENOISE")
            {
                if (isStringNumeric(splitted[1]))
                {
                    int _ImageDenoiseLevel = std::stoi(splitted[1]);
                    if (Camera.CamSensor_id == OV2640_PID)
                    {
                        CameraConfigSaved.ImageDenoiseLevel = 0;
                    }
#if CONFIG_OV3660_SUPPORT || CONFIG_OV5640_SUPPORT
                    if ((Camera.CamSensor_id == OV3660_PID) || (Camera.CamSensor_id == OV5640_PID))
                    {
                        CameraConfigSaved.ImageDenoiseLevel = clipInt(_ImageDenoiseLevel, 8, 0);
                    }
#endif
                }
                existing_parameters = existing_parameters - 1;
            }

            else if (_param == "CAMZOOM")
            {
                CameraConfigSaved.ImageZoomEnabled = alphanumericToBoolean(splitted[1]);
                existing_parameters = existing_parameters - 1;
            }

            else if (_param == "CAMZOOMOFFSETX")
            {
                if (isStringNumeric(splitted[1]))
                {
                    int _ImageZoomOffsetX = std::stoi(splitted[1]);
                    if (Camera.CamSensor_id == OV2640_PID)
                    {
                        CameraConfigSaved.ImageZoomOffsetX = clipInt(_ImageZoomOffsetX, 480, -480);
                    }
#if CONFIG_OV3660_SUPPORT
                    else if (Camera.CamSensor_id == OV3660_PID)
                    {
                        CameraConfigSaved.ImageZoomOffsetX = clipInt(_ImageZoomOffsetX, 704, -704);
                    }
#endif
#if CONFIG_OV5640_SUPPORT
                    else if (Camera.CamSensor_id == OV5640_PID)
                    {
                        CameraConfigSaved.ImageZoomOffsetX = clipInt(_ImageZoomOffsetX, 960, -960);
                    }
#endif
                }
                existing_parameters = existing_parameters - 1;
            }

            else if (_param == "CAMZOOMOFFSETY")
            {
                if (isStringNumeric(splitted[1]))
                {
                    int _ImageZoomOffsetY = std::stoi(splitted[1]);
                    if (Camera.CamSensor_id == OV2640_PID)
                    {
                        CameraConfigSaved.ImageZoomOffsetY = clipInt(_ImageZoomOffsetY, 360, -360);
                    }
#if CONFIG_OV3660_SUPPORT
                    else if (Camera.CamSensor_id == OV3660_PID)
                    {
                        CameraConfigSaved.ImageZoomOffsetY = clipInt(_ImageZoomOffsetY, 528, -528);
                    }
#endif
#if CONFIG_OV5640_SUPPORT
                    else if (Camera.CamSensor_id == OV5640_PID)
                    {
                        CameraConfigSaved.ImageZoomOffsetY = clipInt(_ImageZoomOffsetY, 720, -720);
                    }
#endif
                }
                existing_parameters = existing_parameters - 1;
            }

            else if (_param == "CAMZOOMSIZE")
            {
                if (isStringNumeric(splitted[1]))
                {
                    int _ImageZoomSize = std::stoi(splitted[1]);
                    if (Camera.CamSensor_id == OV2640_PID)
                    {
                        CameraConfigSaved.ImageZoomSize = clipInt(_ImageZoomSize, 29, 0);
                    }
#if CONFIG_OV3660_SUPPORT
                    else if (Camera.CamSensor_id == OV3660_PID)
                    {
                        CameraConfigSaved.ImageZoomSize = clipInt(_ImageZoomSize, 43, 0);
                    }
#endif
#if CONFIG_OV5640_SUPPORT
                    else if (Camera.CamSensor_id == OV5640_PID)
                    {
                        CameraConfigSaved.ImageZoomSize = clipInt(_ImageZoomSize, 59, 0);
                    }
#endif
                }
                existing_parameters = existing_parameters - 1;
            }

            else if (_param == "CAMNEGATIVE")
            {
                CameraConfigSaved.ImageNegative = alphanumericToBoolean(splitted[1]);
                existing_parameters = existing_parameters - 1;
            }

            else if (_param == "LEDINTENSITY")
            {
                if (isStringNumeric(splitted[1]))
                {
                    int ledintensity = std::stoi(splitted[1]);
                    CameraConfigSaved.ImageLedIntensity = Camera.SetLEDIntensity(ledintensity);
                }
                existing_parameters = existing_parameters - 1;
            }

            else if (_param == "DEMO")
            {
                Camera.DemoMode = alphanumericToBoolean(splitted[1]);
                if (Camera.DemoMode == true)
                {
                    Camera.useDemoMode();
                }
                existing_parameters = existing_parameters - 1;
            }
        }
    }

    if (existing_parameters > 0)
    {
        LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "Your config.ini does not correspond to this version.");
        LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "%d Parameters are missing in the section [TakeImage] or they are commented out.", existing_parameters);
        LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "The default values ​​are used for these Parameters!");
    }
    else if (existing_parameters < 0)
    {
        LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "Your config.ini does not correspond to this version.");
        LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "%d Parameters are too much in the section [TakeImage].", abs(existing_parameters));
    }

    if (!Camera.CamInitSuccessful)
    {
        Camera.DemoMode = true;
        Camera.useDemoMode();
    }

    Camera.SetSensorControllConfig(&CameraConfigSaved); // CameraConfigSaved >>> Kamera
    Camera.SetQualityZoomSize(&CameraConfigSaved);

    Camera.LedIntensity = CameraConfigSaved.ImageLedIntensity;
    Camera.CamSettingsChanged = false;
    Camera.CamTempImage = false;

    rawImage = new CImageBasis("rawImage");
    rawImage->CreateEmptyImage(CameraConfigSaved.ImageWidth, CameraConfigSaved.ImageHeight, 3);

    return true;
}

ClassFlowTakeImage::ClassFlowTakeImage(std::vector<ClassFlow *> *lfc) : ClassFlowImage(lfc, TAG)
{
    imagesLocation = "/log/source";
    imagesRetention = 5;
    SetInitialParameter();
}

std::string ClassFlowTakeImage::getHTMLSingleStep(std::string host)
{
    std::string result;
    result = "Raw Image: <br>\n<img src=\"" + host + "/img_tmp/raw.jpg\">\n";
    return result;
}

// wird bei jeder Auswertrunde aufgerufen
bool ClassFlowTakeImage::doFlow(std::string time_temp)
{
    psram_init_shared_memory_for_take_image_step();
    std::string logPath = CreateLogFolder(time_temp);

    int flash_duration = (int)(CameraConfigSaved.WaitBeforePicture * 1000);
    if (Camera.CamTempImage)
    {
        flash_duration = (int)(CameraConfigTemp.WaitBeforePicture * 1000);
    }

#ifdef WIFITURNOFF
    esp_wifi_stop(); // to save power usage and
#endif

    takePictureWithFlash(flash_duration);

#ifdef WIFITURNOFF
    esp_wifi_start();
#endif

    LogImage(logPath, "raw", NULL, NULL, time_temp, rawImage);
    RemoveOldLogs();
    psram_deinit_shared_memory_for_take_image_step();

    return true;
}

esp_err_t ClassFlowTakeImage::camera_capture(void)
{
    Camera.CaptureToFile(pathRawImage);
    time(&TimeImageTaken);
    localtime(&TimeImageTaken);

    return ESP_OK;
}

void ClassFlowTakeImage::takePictureWithFlash(int flash_duration)
{
    // in case the image is flipped, it must be reset here
    rawImage->width = CameraConfigSaved.ImageWidth;
    rawImage->height = CameraConfigSaved.ImageHeight;
    if (Camera.CamTempImage)
    {
        rawImage->width = CameraConfigTemp.ImageWidth;
        rawImage->height = CameraConfigTemp.ImageHeight;
    }

    ESP_LOGD(TAG, "flash_duration: %d", flash_duration);

    Camera.CaptureToBasisImage(rawImage, flash_duration);

    time(&TimeImageTaken);
    localtime(&TimeImageTaken);

    if (Camera.SaveAllFiles)
    {
        rawImage->SaveToFile(pathRawImage);
    }
}

esp_err_t ClassFlowTakeImage::SendRawJPG(httpd_req_t *req)
{
    int flash_duration = (int)(CameraConfigSaved.WaitBeforePicture * 1000);
    if (Camera.CamTempImage)
    {
        flash_duration = (int)(CameraConfigTemp.WaitBeforePicture * 1000);
    }

    time(&TimeImageTaken);
    localtime(&TimeImageTaken);

    return Camera.CaptureToHTTP(req, flash_duration);
}

ImageData *ClassFlowTakeImage::SendRawImage(void)
{
    CImageBasis *image = new CImageBasis("SendRawImage", rawImage);

    int flash_duration = (int)(CameraConfigSaved.WaitBeforePicture * 1000);
    if (Camera.CamTempImage)
    {
        flash_duration = (int)(CameraConfigTemp.WaitBeforePicture * 1000);
    }

    Camera.CaptureToBasisImage(image, flash_duration);

    time(&TimeImageTaken);
    localtime(&TimeImageTaken);

    ImageData *image_data = image->writeToMemoryAsJPG();
    delete image;

    return image_data;
}

time_t ClassFlowTakeImage::getTimeImageTaken(void)
{
    return TimeImageTaken;
}

ClassFlowTakeImage::~ClassFlowTakeImage(void)
{
    delete rawImage;
}
