#include "server_camera.h"

#include <string>
#include <string.h>
#include <esp_log.h>
#include <esp_vfs.h>
#include <esp_camera.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/event_groups.h>

#include "defines.h"
#include "Helper.h"

#include "server_main.h"
#include "server_file.h"
#include "server_help.h"

#include "ClassControllCamera.h"
#include "MainFlowControl.h"
#include "ClassLogFile.h"

#include "basic_auth.h"

static const char *TAG = "server_cam";

static esp_err_t lightOn_handler(httpd_req_t *req)
{
    set_deep_sleep_state(false);
    esp_err_t ret = ESP_OK;

    if (Camera.getCameraInitSuccessful())
    {
        Camera.FlashLightOnOff(true, Camera.LedIntensity);
        const char *resp_str = (const char *)req->user_ctx;
        ret &= httpd_resp_send(req, resp_str, strlen(resp_str));
    }
    else
    {
        ret &= httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Camera not initialized: REST API /lighton not available!");
    }
    ret &= httpd_resp_send_chunk(req, NULL, 0);

    return ret;
}

static esp_err_t lightOff_handler(httpd_req_t *req)
{
    set_deep_sleep_state(false);
    esp_err_t ret = ESP_OK;

    if (Camera.getCameraInitSuccessful())
    {
        Camera.FlashLightOnOff(false, Camera.LedIntensity);
        const char *resp_str = (const char *)req->user_ctx;
        ret &= httpd_resp_send(req, resp_str, strlen(resp_str));
    }
    else
    {
        ret &= httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Camera not initialized: REST API /lightoff not available!");
    }
    ret &= httpd_resp_send_chunk(req, NULL, 0);

    return ret;
}

static esp_err_t stream_handler(httpd_req_t *req)
{
    set_deep_sleep_state(false);
    esp_err_t ret = ESP_OK;

    if (Camera.getCameraInitSuccessful())
    {
        char _query[50];
        char _value[10];
        bool flashlightOn = false;

        if (httpd_req_get_url_query_str(req, _query, sizeof(_query)) == ESP_OK)
        {
            if (httpd_query_key_value(_query, "flashlight", _value, sizeof(_value)) == ESP_OK)
            {
                if (strlen(_value) > 0)
                {
                    flashlightOn = true;
                }
            }
        }

        ret &= Camera.CaptureToStream(req, flashlightOn);
    }
    else
    {
        ret &= httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Camera not initialized: REST API /stream not available!");
    }
    ret &= httpd_resp_send_chunk(req, NULL, 0);

    return ret;
}

static esp_err_t capture_handler(httpd_req_t *req)
{
    set_deep_sleep_state(false);
    esp_err_t ret = ESP_OK;

    if (Camera.getCameraInitSuccessful())
    {
        ret &= Camera.CaptureToHTTP(req);
    }
    else
    {
        ret &= httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Camera not initialized: REST API /capture not available!");
    }
    ret &= httpd_resp_send_chunk(req, NULL, 0);

    return ret;
}

static esp_err_t capture_with_light_handler(httpd_req_t *req)
{
    set_deep_sleep_state(false);
    esp_err_t ret = ESP_OK;

    if (Camera.getCameraInitSuccessful())
    {
        char _query[100];
        char _delay[10];
        int delay = (int)(CameraConfigSaved.WaitBeforePicture * 1000);

        if (httpd_req_get_url_query_str(req, _query, sizeof(_query)) == ESP_OK)
        {
            ESP_LOGD(TAG, "Query: %s", _query);
            if (httpd_query_key_value(_query, "delay", _delay, sizeof(_delay)) == ESP_OK)
            {
                delay = atoi(_delay);

                if (delay < 0)
                {
                    delay = 0;
                }
            }
        }

        ret &= Camera.CaptureToHTTP(req, delay);
    }
    else
    {
        ret &= httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Camera not initialized: REST API /capture_with_flashlight not available!");
    }
    ret &= httpd_resp_send_chunk(req, NULL, 0);

    return ret;
}

static esp_err_t capture_save_to_file_handler(httpd_req_t *req)
{
    set_deep_sleep_state(false);
    esp_err_t ret = ESP_OK;

    if (Camera.getCameraInitSuccessful())
    {
        char _query[100];
        char _delay[10];
        int delay = 0;
        char filename[100];
        std::string fn = "/sdcard/";

        if (httpd_req_get_url_query_str(req, _query, sizeof(_query)) == ESP_OK)
        {
            ESP_LOGD(TAG, "Query: %s", _query);
            if (httpd_query_key_value(_query, "filename", filename, sizeof(filename)) == ESP_OK)
            {
                fn.append(filename);
            }
            else
            {
                fn.append("noname.jpg");
            }

            if (httpd_query_key_value(_query, "delay", _delay, sizeof(_delay)) == ESP_OK)
            {
                delay = atoi(_delay);
                if (delay < 0)
                {
                    delay = 0;
                }
            }
        }
        else
        {
            fn.append("noname.jpg");
        }

        ret = Camera.CaptureToFile(fn, delay);

        const char *resp_str = (const char *)fn.c_str();
        ret &= httpd_resp_send(req, resp_str, strlen(resp_str));
    }
    else
    {
        ret &= httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Camera not initialized: REST API /save not available!");
    }
    ret &= httpd_resp_send_chunk(req, NULL, 0);

    return ret;
}

static esp_err_t edit_camera_config_handler(httpd_req_t *req)
{
    set_deep_sleep_state(false);

    ESP_LOGD(TAG, "handler_editflow uri: %s", req->uri);

    char _query[768];
    char _valuechar[30];

    if (httpd_req_get_url_query_str(req, _query, sizeof(_query)) == ESP_OK)
    {
        if (httpd_query_key_value(_query, "task", _valuechar, sizeof(_valuechar)) == ESP_OK)
        {
            std::string _task = std::string(toUpper(_valuechar));

            // wird beim Erstellen eines neuen Referenzbildes aufgerufen
            std::string *sys_status = flowctrl.getActStatus();

            if ((_task.compare("TEST_TAKE") == 0) || (_task.compare("CAM_SETTINGS") == 0))
            {
                if ((sys_status->c_str() != std::string("Take Image")) && (sys_status->c_str() != std::string("Aligning")))
                {
                    std::string temp_host = "";

                    // laden der aktuellen Kameraeinstellungen(CameraConfigSaved) in den Zwischenspeicher(CameraConfigTemp)
                    Camera.SetCameraConfigFromTo(&CameraConfigSaved, &CameraConfigTemp);

                    if (httpd_query_key_value(_query, "host", _valuechar, sizeof(_valuechar)) == ESP_OK)
                    {
                        temp_host = std::string(_valuechar);
                    }

                    if (httpd_query_key_value(_query, "waitb", _valuechar, sizeof(_valuechar)) == ESP_OK)
                    {
                        std::string temp_waitb = std::string(_valuechar);
                        if (isStringNumeric(temp_waitb))
                        {
                            if (std::stoi(temp_waitb) >= 0)
                            {
                                CameraConfigTemp.WaitBeforePicture = std::stoi(temp_waitb);
                            }
                        }
                    }

                    if (httpd_query_key_value(_query, "xclk", _valuechar, sizeof(_valuechar)) == ESP_OK)
                    {
                        std::string temp_xclk = std::string(_valuechar);
                        if (isStringNumeric(temp_xclk))
                        {
                            int temp_xclk_ = std::stoi(_valuechar);
                            CameraConfigTemp.CamXclkFreqMhz = clipInt(temp_xclk_, 20, 1);
                        }
                    }

                    if (httpd_query_key_value(_query, "aecgc", _valuechar, sizeof(_valuechar)) == ESP_OK)
                    {
                        std::string temp_aecgc = std::string(toUpper(_valuechar));
                        // ESP_LOGI(TAG, "ImageGainceiling = %s", _aecgc.c_str());
                        if (isStringNumeric(temp_aecgc))
                        {
                            int temp_aecgc_ = std::stoi(_valuechar);
                            CameraConfigTemp.ImageGainceiling = clipInt(temp_aecgc_, 7, 0);
                        }
                        else
                        {
                            if (temp_aecgc == "X2")
                            {
                                CameraConfigTemp.ImageGainceiling = 0;
                            }
                            else if (temp_aecgc == "X4")
                            {
                                CameraConfigTemp.ImageGainceiling = 1;
                            }
                            else if (temp_aecgc == "X8")
                            {
                                CameraConfigTemp.ImageGainceiling = 2;
                            }
                            else if (temp_aecgc == "X16")
                            {
                                CameraConfigTemp.ImageGainceiling = 3;
                            }
                            else if (temp_aecgc == "X32")
                            {
                                CameraConfigTemp.ImageGainceiling = 5;
                            }
                            else if (temp_aecgc == "X64")
                            {
                                CameraConfigTemp.ImageGainceiling = 5;
                            }
                            else if (temp_aecgc == "X128")
                            {
                                CameraConfigTemp.ImageGainceiling = 6;
                            }
                            else if (temp_aecgc == "X256")
                            {
                                CameraConfigTemp.ImageGainceiling = 7;
                            }
                        }
                    }

                    if (httpd_query_key_value(_query, "qual", _valuechar, sizeof(_valuechar)) == ESP_OK)
                    {
                        std::string temp_qual = std::string(_valuechar);
                        if (isStringNumeric(temp_qual))
                        {
                            int temp_qual_ = std::stoi(_valuechar);
                            CameraConfigTemp.ImageQuality = clipInt(temp_qual_, 63, 6);
                        }
                    }

                    if (httpd_query_key_value(_query, "bri", _valuechar, sizeof(_valuechar)) == ESP_OK)
                    {
                        std::string temp_bri = std::string(_valuechar);
                        if (isStringNumeric(temp_bri))
                        {
                            int temp_bri_ = std::stoi(_valuechar);
                            CameraConfigTemp.ImageBrightness = clipInt(temp_bri_, 2, -2);
                        }
                    }

                    if (httpd_query_key_value(_query, "con", _valuechar, sizeof(_valuechar)) == ESP_OK)
                    {
                        std::string temp_con = std::string(_valuechar);
                        if (isStringNumeric(temp_con))
                        {
                            int temp_con_ = std::stoi(_valuechar);
                            CameraConfigTemp.ImageContrast = clipInt(temp_con_, 2, -2);
                        }
                    }

                    if (httpd_query_key_value(_query, "sat", _valuechar, sizeof(_valuechar)) == ESP_OK)
                    {
                        std::string temp_sat = std::string(_valuechar);
                        if (isStringNumeric(temp_sat))
                        {
                            int temp_sat_ = std::stoi(_valuechar);
                            CameraConfigTemp.ImageSaturation = clipInt(temp_sat_, 2, -2);
                        }
                    }

                    if (httpd_query_key_value(_query, "shp", _valuechar, sizeof(_valuechar)) == ESP_OK)
                    {
                        std::string temp_shp = std::string(_valuechar);
                        if (isStringNumeric(temp_shp))
                        {
                            int temp_shp_ = std::stoi(_valuechar);
                            if (Camera.CamSensor_id == OV2640_PID)
                            {
                                CameraConfigTemp.ImageSharpness = clipInt(temp_shp_, 2, -2);
                            }
#if CONFIG_OV3660_SUPPORT || CONFIG_OV5640_SUPPORT
                            else if ((Camera.CamSensor_id == OV3660_PID) || (Camera.CamSensor_id == OV5640_PID))
                            {
                                CameraConfigTemp.ImageSharpness = clipInt(temp_shp_, 3, -3);
                            }
#endif
                        }
                    }

                    if (httpd_query_key_value(_query, "ashp", _valuechar, sizeof(_valuechar)) == ESP_OK)
                    {
                        std::string temp_ashp = std::string(_valuechar);
                        CameraConfigTemp.ImageAutoSharpness = alphanumericToBoolean(temp_ashp);
                    }

                    if (httpd_query_key_value(_query, "spe", _valuechar, sizeof(_valuechar)) == ESP_OK)
                    {
                        std::string temp_spe = std::string(toUpper(_valuechar));
                        if (isStringNumeric(temp_spe))
                        {
                            int temp_spe_ = std::stoi(_valuechar);
                            CameraConfigTemp.ImageSpecialEffect = clipInt(temp_spe_, 6, 0);
                        }
                        else
                        {
                            if (temp_spe == "NO_EFFECT")
                            {
                                CameraConfigTemp.ImageSpecialEffect = 0;
                            }
                            else if (temp_spe == "NEGATIVE")
                            {
                                CameraConfigTemp.ImageSpecialEffect = 1;
                            }
                            else if (temp_spe == "GRAYSCALE")
                            {
                                CameraConfigTemp.ImageSpecialEffect = 2;
                            }
                            else if (temp_spe == "RED")
                            {
                                CameraConfigTemp.ImageSpecialEffect = 3;
                            }
                            else if (temp_spe == "GREEN")
                            {
                                CameraConfigTemp.ImageSpecialEffect = 4;
                            }
                            else if (temp_spe == "BLUE")
                            {
                                CameraConfigTemp.ImageSpecialEffect = 5;
                            }
                            else if (temp_spe == "RETRO")
                            {
                                CameraConfigTemp.ImageSpecialEffect = 6;
                            }
                        }
                    }

                    if (httpd_query_key_value(_query, "wbm", _valuechar, sizeof(_valuechar)) == ESP_OK)
                    {
                        std::string temp_wbm = std::string(toUpper(_valuechar));
                        if (isStringNumeric(temp_wbm))
                        {
                            int temp_wbm_ = std::stoi(_valuechar);
                            CameraConfigTemp.ImageWbMode = clipInt(temp_wbm_, 4, 0);
                        }
                        else
                        {
                            if (temp_wbm == "AUTO")
                            {
                                CameraConfigTemp.ImageWbMode = 0;
                            }
                            else if (temp_wbm == "SUNNY")
                            {
                                CameraConfigTemp.ImageWbMode = 1;
                            }
                            else if (temp_wbm == "CLOUDY")
                            {
                                CameraConfigTemp.ImageWbMode = 2;
                            }
                            else if (temp_wbm == "OFFICE")
                            {
                                CameraConfigTemp.ImageWbMode = 3;
                            }
                            else if (temp_wbm == "HOME")
                            {
                                CameraConfigTemp.ImageWbMode = 4;
                            }
                        }
                    }

                    if (httpd_query_key_value(_query, "awb", _valuechar, sizeof(_valuechar)) == ESP_OK)
                    {
                        std::string temp_awb = std::string(_valuechar);
                        CameraConfigTemp.ImageAwb = alphanumericToInteger(temp_awb);
                    }

                    if (httpd_query_key_value(_query, "awbg", _valuechar, sizeof(_valuechar)) == ESP_OK)
                    {
                        std::string temp_awbg = std::string(_valuechar);
                        CameraConfigTemp.ImageAwbGain = alphanumericToInteger(temp_awbg);
                    }

                    if (httpd_query_key_value(_query, "aec", _valuechar, sizeof(_valuechar)) == ESP_OK)
                    {
                        std::string temp_aec = std::string(_valuechar);
                        CameraConfigTemp.ImageAec = alphanumericToInteger(temp_aec);
                    }

                    if (httpd_query_key_value(_query, "aec2", _valuechar, sizeof(_valuechar)) == ESP_OK)
                    {
                        std::string temp_aec2 = std::string(_valuechar);
                        CameraConfigTemp.ImageAec2 = alphanumericToInteger(temp_aec2);
                    }

                    if (httpd_query_key_value(_query, "ael", _valuechar, sizeof(_valuechar)) == ESP_OK)
                    {
                        std::string temp_ael = std::string(_valuechar);
                        if (isStringNumeric(temp_ael))
                        {
                            int temp_ael_ = std::stoi(_valuechar);
                            if (Camera.CamSensor_id == OV2640_PID)
                            {
                                CameraConfigTemp.ImageAeLevel = clipInt(temp_ael_, 2, -2);
                            }
#if CONFIG_OV3660_SUPPORT || CONFIG_OV5640_SUPPORT
                            else if ((Camera.CamSensor_id == OV3660_PID) || (Camera.CamSensor_id == OV5640_PID))
                            {
                                CameraConfigTemp.ImageAeLevel = clipInt(temp_ael_, 5, -5);
                            }
#endif
                        }
                    }

                    if (httpd_query_key_value(_query, "aecv", _valuechar, sizeof(_valuechar)) == ESP_OK)
                    {
                        std::string temp_aecv = std::string(_valuechar);
                        if (isStringNumeric(temp_aecv))
                        {
                            int temp_aecv_ = std::stoi(_valuechar);
                            CameraConfigTemp.ImageAecValue = clipInt(temp_aecv_, 1200, 0);
                        }
                    }

                    if (httpd_query_key_value(_query, "agc", _valuechar, sizeof(_valuechar)) == ESP_OK)
                    {
                        std::string temp_agc = std::string(_valuechar);
                        CameraConfigTemp.ImageAgc = alphanumericToInteger(temp_agc);
                    }

                    if (httpd_query_key_value(_query, "agcg", _valuechar, sizeof(_valuechar)) == ESP_OK)
                    {
                        std::string temp_agcg = std::string(_valuechar);
                        if (isStringNumeric(temp_agcg))
                        {
                            int temp_agcg_ = std::stoi(_valuechar);
                            CameraConfigTemp.ImageAgcGain = clipInt(temp_agcg_, 30, 0);
                        }
                    }

                    if (httpd_query_key_value(_query, "bpc", _valuechar, sizeof(_valuechar)) == ESP_OK)
                    {
                        std::string temp_bpc = std::string(_valuechar);
                        CameraConfigTemp.ImageBpc = alphanumericToInteger(temp_bpc);
                    }

                    if (httpd_query_key_value(_query, "wpc", _valuechar, sizeof(_valuechar)) == ESP_OK)
                    {
                        std::string temp_wpc = std::string(_valuechar);
                        CameraConfigTemp.ImageWpc = alphanumericToInteger(temp_wpc);
                    }

                    if (httpd_query_key_value(_query, "rgma", _valuechar, sizeof(_valuechar)) == ESP_OK)
                    {
                        std::string temp_rgma = std::string(_valuechar);
                        CameraConfigTemp.ImageRawGma = alphanumericToInteger(temp_rgma);
                    }

                    if (httpd_query_key_value(_query, "lenc", _valuechar, sizeof(_valuechar)) == ESP_OK)
                    {
                        std::string temp_lenc = std::string(_valuechar);
                        CameraConfigTemp.ImageLenc = alphanumericToInteger(temp_lenc);
                    }

                    if (httpd_query_key_value(_query, "mirror", _valuechar, sizeof(_valuechar)) == ESP_OK)
                    {
                        std::string temp_mirror = std::string(_valuechar);
                        CameraConfigTemp.ImageHmirror = alphanumericToInteger(temp_mirror);
                    }

                    if (httpd_query_key_value(_query, "flip", _valuechar, sizeof(_valuechar)) == ESP_OK)
                    {
                        std::string temp_flip = std::string(_valuechar);
                        CameraConfigTemp.ImageVflip = alphanumericToInteger(temp_flip);
                    }

                    if (httpd_query_key_value(_query, "dcw", _valuechar, sizeof(_valuechar)) == ESP_OK)
                    {
                        std::string temp_dcw = std::string(_valuechar);
                        CameraConfigTemp.ImageDcw = alphanumericToInteger(temp_dcw);
                    }

                    if (httpd_query_key_value(_query, "den", _valuechar, sizeof(_valuechar)) == ESP_OK)
                    {
                        std::string temp_idlv = std::string(_valuechar);
                        if (isStringNumeric(temp_idlv))
                        {
                            int _ImageDenoiseLevel = std::stoi(_valuechar);
                            if (Camera.CamSensor_id == OV2640_PID)
                            {
                                CameraConfigTemp.ImageDenoiseLevel = 0;
                            }
#if CONFIG_OV3660_SUPPORT || CONFIG_OV5640_SUPPORT
                            else if ((Camera.CamSensor_id == OV3660_PID) || (Camera.CamSensor_id == OV5640_PID))
                            {
                                CameraConfigTemp.ImageDenoiseLevel = clipInt(_ImageDenoiseLevel, 8, 0);
                            }
#endif
                        }
                    }

                    if (httpd_query_key_value(_query, "zoom", _valuechar, sizeof(_valuechar)) == ESP_OK)
                    {
                        std::string temp_zoom = std::string(_valuechar);
                        CameraConfigTemp.ImageZoomEnabled = alphanumericToBoolean(temp_zoom);
                    }

                    if (httpd_query_key_value(_query, "zoomx", _valuechar, sizeof(_valuechar)) == ESP_OK)
                    {
                        std::string temp_zoomx = std::string(_valuechar);
                        if (isStringNumeric(temp_zoomx))
                        {
                            int _ImageZoomOffsetX = std::stoi(_valuechar);
                            if (Camera.CamSensor_id == OV2640_PID)
                            {
                                CameraConfigTemp.ImageZoomOffsetX = clipInt(_ImageZoomOffsetX, 480, -480);
                            }
#if CONFIG_OV3660_SUPPORT
                            else if (Camera.CamSensor_id == OV3660_PID)
                            {
                                CameraConfigTemp.ImageZoomOffsetX = clipInt(_ImageZoomOffsetX, 704, -704);
                            }
#endif
#if CONFIG_OV5640_SUPPORT
                            else if (Camera.CamSensor_id == OV5640_PID)
                            {
                                CameraConfigTemp.ImageZoomOffsetX = clipInt(_ImageZoomOffsetX, 960, -960);
                            }
#endif
                        }
                    }

                    if (httpd_query_key_value(_query, "zoomy", _valuechar, sizeof(_valuechar)) == ESP_OK)
                    {
                        std::string temp_zoomy = std::string(_valuechar);
                        if (isStringNumeric(temp_zoomy))
                        {
                            int _ImageZoomOffsetY = std::stoi(_valuechar);
                            if (Camera.CamSensor_id == OV2640_PID)
                            {
                                CameraConfigTemp.ImageZoomOffsetY = clipInt(_ImageZoomOffsetY, 360, -360);
                            }
#if CONFIG_OV3660_SUPPORT
                            else if (Camera.CamSensor_id == OV3660_PID)
                            {
                                CameraConfigTemp.ImageZoomOffsetY = clipInt(_ImageZoomOffsetY, 528, -528);
                            }
#endif
#if CONFIG_OV5640_SUPPORT
                            else if (Camera.CamSensor_id == OV5640_PID)
                            {
                                CameraConfigTemp.ImageZoomOffsetY = clipInt(_ImageZoomOffsetY, 720, -720);
                            }
#endif
                        }
                    }

                    if (httpd_query_key_value(_query, "zooms", _valuechar, sizeof(_valuechar)) == ESP_OK)
                    {
                        std::string temp_zooms = std::string(_valuechar);
                        if (isStringNumeric(temp_zooms))
                        {
                            int _ImageZoomSize = std::stoi(_valuechar);
                            if (Camera.CamSensor_id == OV2640_PID)
                            {
                                CameraConfigTemp.ImageZoomSize = clipInt(_ImageZoomSize, 29, 0);
                            }
#if CONFIG_OV3660_SUPPORT
                            else if (Camera.CamSensor_id == OV3660_PID)
                            {
                                CameraConfigTemp.ImageZoomSize = clipInt(_ImageZoomSize, 43, 0);
                            }
#endif
#if CONFIG_OV5640_SUPPORT
                            else if (Camera.CamSensor_id == OV5640_PID)
                            {
                                CameraConfigTemp.ImageZoomSize = clipInt(_ImageZoomSize, 59, 0);
                            }
#endif
                        }
                    }

                    if (httpd_query_key_value(_query, "invert", _valuechar, sizeof(_valuechar)) == ESP_OK)
                    {
                        std::string temp_invert = std::string(_valuechar);
                        CameraConfigTemp.ImageNegative = alphanumericToBoolean(temp_invert);
                    }

                    if (httpd_query_key_value(_query, "ledi", _valuechar, sizeof(_valuechar)) == ESP_OK)
                    {
                        std::string temp_ledi = std::string(_valuechar);
                        if (isStringNumeric(temp_ledi))
                        {
                            int _ImageLedIntensity = std::stoi(_valuechar);
                            CameraConfigTemp.ImageLedIntensity = Camera.SetLEDIntensity(_ImageLedIntensity);
                        }
                    }

                    if (_task.compare("CAM_SETTINGS") == 0)
                    {
                        // wird aufgerufen, wenn das Referenzbild + Kameraeinstellungen gespeichert wurden
                        Camera.SetCameraConfigFromTo(&CameraConfigTemp, &CameraConfigSaved);

                        // Kameraeinstellungen wurden verädert
                        Camera.CamSettingsChanged = true;
                        Camera.CamTempImage = false;

                        ESP_LOGD(TAG, "Cam Settings set");
                        std::string temp_bufer = "CamSettingsSet";
                        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
                        httpd_resp_send(req, temp_bufer.c_str(), temp_bufer.length());
                    }
                    else
                    {
                        // wird aufgerufen, wenn ein neues Referenzbild erstellt oder aktualisiert wurde
                        // Kameraeinstellungen wurden verädert
                        Camera.CamSettingsChanged = true;
                        Camera.CamTempImage = true;

                        ESP_LOGD(TAG, "test_take - vor TakeImage");
                        std::string image_temp = flowctrl.doSingleStep("[TakeImage]", temp_host);
                        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
                        httpd_resp_send(req, image_temp.c_str(), image_temp.length());
                    }
                }
                else
                {
                    std::string temp_bufer = "DeviceIsBusy";
                    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
                    httpd_resp_send(req, temp_bufer.c_str(), temp_bufer.length());
                }
            }
            else
            {
                httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Not a valid request(task incorrect)!");
                // Respond with an empty chunk to signal HTTP response completion
                httpd_resp_send_chunk(req, NULL, 0);
                return ESP_FAIL;
            }
        }
        else
        {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Not a valid request(task empty)!");
            // Respond with an empty chunk to signal HTTP response completion
            httpd_resp_send_chunk(req, NULL, 0);
            return ESP_FAIL;
        }
    }
    else
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Not a valid request(request empty)!");
        // Respond with an empty chunk to signal HTTP response completion
        httpd_resp_send_chunk(req, NULL, 0);
        return ESP_FAIL;
    }

    // Respond with an empty chunk to signal HTTP response completion
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

esp_err_t register_server_camera_uri(httpd_handle_t my_server)
{
    esp_err_t ret = ESP_OK;

    httpd_uri_t lighton = {
        .uri = "/lighton",
        .method = HTTP_GET,
        .handler = APPLY_BASIC_AUTH_FILTER(lightOn_handler),
        .user_ctx = rest_context // Pass server data as context
    };
    ret |= httpd_register_uri_handler(my_server, &lighton);

    httpd_uri_t lightoff = {
        .uri = "/lightoff",
        .method = HTTP_GET,
        .handler = APPLY_BASIC_AUTH_FILTER(lightOff_handler),
        .user_ctx = rest_context // Pass server data as context
    };
    ret |= httpd_register_uri_handler(my_server, &lightoff);

    httpd_uri_t stream = {
        .uri = "/stream",
        .method = HTTP_GET,
        .handler = APPLY_BASIC_AUTH_FILTER(stream_handler),
        .user_ctx = rest_context // Pass server data as context
    };
    ret |= httpd_register_uri_handler(my_server, &stream);

    httpd_uri_t capture = {
        .uri = "/capture",
        .method = HTTP_GET,
        .handler = APPLY_BASIC_AUTH_FILTER(capture_handler),
        .user_ctx = rest_context // Pass server data as context
    };
    ret |= httpd_register_uri_handler(my_server, &capture);

    httpd_uri_t capture_with_flashlight = {
        .uri = "/capture_with_flashlight",
        .method = HTTP_GET,
        .handler = APPLY_BASIC_AUTH_FILTER(capture_with_light_handler),
        .user_ctx = rest_context // Pass server data as context
    };
    ret |= httpd_register_uri_handler(my_server, &capture_with_flashlight);

    httpd_uri_t save = {
        .uri = "/save_image",
        .method = HTTP_GET,
        .handler = APPLY_BASIC_AUTH_FILTER(capture_save_to_file_handler),
        .user_ctx = rest_context // Pass server data as context
    };
    ret |= httpd_register_uri_handler(my_server, &save);

    httpd_uri_t camera_config = {
        .uri = "/camera_config",
        .method = HTTP_GET,
        .handler = APPLY_BASIC_AUTH_FILTER(edit_camera_config_handler),
        .user_ctx = rest_context // Pass server data as context
    };
    ret |= httpd_register_uri_handler(my_server, &camera_config);

    return ret;
}
