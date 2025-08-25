#include "MainFlowControl.h"

#include <string>
#include <vector>
#include <string.h>
#include <esp_log.h>
#include <esp_timer.h>

#include <iomanip>
#include <sstream>

#include <esp_sleep.h>
#include <esp_wake_stub.h>
#include <driver/rtc_io.h>

#include "defines.h"
#include "Helper.h"
#include "statusled.h"

#include "esp_camera.h"
#include "time_sntp.h"
#include "ClassControllCamera.h"

#include "ClassFlowControll.h"

#include "ClassLogFile.h"
#include "server_GpioPin.h"
#include "server_GpioHandler.h"

#include "server_file.h"
#include "server_help.h"

#include "read_network_config.h"
#include "connect_wifi_sta.h"
#if (CONFIG_ETH_ENABLED && CONFIG_ETH_USE_SPI_ETHERNET && CONFIG_ETH_SPI_ETHERNET_W5500)
#include "connect_eth.h"
#endif

#include "psram.h"
#include "basic_auth.h"

// support IDF 5.x
#ifndef portTICK_RATE_MS
#define portTICK_RATE_MS portTICK_PERIOD_MS
#endif

ClassFlowControll flowctrl;

TaskHandle_t xHandletask_autodoFlow = NULL;

bool bTaskAutoFlowCreated = false;
bool flowisrunning = false;

long auto_interval = 0;
bool autostartIsEnabled = false;

bool isPlannedReboot = false;

bool deep_sleep_state = true;
#if SOC_RTC_FAST_MEM_SUPPORTED
// countRounds stored in RTC memory
static RTC_DATA_ATTR int countRounds = 0;
#else
int countRounds = 0;
#endif

static const char *TAG = "MAINCTRL";

// #define DEBUG_DETAIL_ON

bool get_deep_sleep_state(void)
{
    return deep_sleep_state;
}

void set_deep_sleep_state(bool _deep_sleep_state)
{
    deep_sleep_state = _deep_sleep_state;
}

long get_auto_interval(void)
{
    return auto_interval;
}

void set_auto_interval(long _auto_interval)
{
    auto_interval = _auto_interval;
}

void CheckIsPlannedReboot(void)
{
    FILE *pFile;

    if ((pFile = fopen("/sdcard/reboot.txt", "r")) == NULL)
    {
        isPlannedReboot = false;
    }
    else
    {
        LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "Planned reboot");
        DeleteFile("/sdcard/reboot.txt"); // Prevent Boot Loop!!!
        isPlannedReboot = true;
    }
}

bool getIsPlannedReboot(void)
{
    return isPlannedReboot;
}

int getCountFlowRounds(void)
{
    return countRounds;
}

esp_err_t GetJPG(std::string _filename, httpd_req_t *req)
{
    return flowctrl.GetJPGStream(_filename, req);
}

esp_err_t GetRawJPG(httpd_req_t *req)
{
    return flowctrl.SendRawJPG(req);
}

bool isSetupModusActive(void)
{
    return flowctrl.getStatusSetupModus();
}

void DeleteMainFlowTask(void)
{
    if (xHandletask_autodoFlow != NULL)
    {
        vTaskDelete(xHandletask_autodoFlow);
        xHandletask_autodoFlow = NULL;
    }
}

void system_init(void)
{
    flowctrl.InitFlow(CONFIG_FILE);

    /* GPIO handler has to be initialized before MQTT init to ensure proper topic subscription */
    gpio_handler_init();

#ifdef ENABLE_MQTT
    flowctrl.StartMQTTService();
#endif // ENABLE_MQTT
}

bool doflow(void)
{
    std::string time_temp = getCurrentTimeString(LOGFILE_TIME_FORMAT);
    ESP_LOGD(TAG, "doflow - start %s", time_temp.c_str());
    flowisrunning = true;
    flowctrl.doFlow(time_temp);
    flowisrunning = false;

    return true;
}

#ifdef ENABLE_MQTT
esp_err_t MQTTCtrlFlowStart(std::string _topic)
{
    set_deep_sleep_state(false);

    ESP_LOGD(TAG, "MQTTCtrlFlowStart: topic %s", _topic.c_str());

    if (autostartIsEnabled)
    {
        xTaskAbortDelay(xHandletask_autodoFlow); // Delay will be aborted if task is in blocked (waiting) state. If task is already running, no action
        LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "Flow start triggered by MQTT topic " + _topic);
    }
    else
    {
        LogFile.WriteToFile(ESP_LOG_WARN, TAG, "Flow start triggered by MQTT topic " + _topic + ", but flow is not active!");
    }

    return ESP_OK;
}
#endif // ENABLE_MQTT

void task_autodoFlow(void *pvParameter)
{
    int64_t fr_start, fr_delta_ms;
    bTaskAutoFlowCreated = true;

    if (!isPlannedReboot && (esp_reset_reason() == ESP_RST_PANIC))
    {
        flowctrl.setActStatus("Initialization (delayed)");
        vTaskDelay(60 * 5000 / portTICK_PERIOD_MS); // Wait 5 minutes to give time to do an OTA update or fetch the log
    }

    ESP_LOGD(TAG, "task_autodoFlow: start");
    system_init();

    flowctrl.setAutoStartInterval(auto_interval);
    autostartIsEnabled = flowctrl.getIsAutoStart();

    if (isSetupModusActive())
    {
        LogFile.WriteToFile(ESP_LOG_INFO, TAG, "We are in Setup Mode -> Not starting Auto Flow!");
        autostartIsEnabled = false;
    }

    if (autostartIsEnabled)
    {
        LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Starting Flow...");
    }
    else
    {
        LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Autostart is not enabled -> Not starting Flow");
    }

    while (autostartIsEnabled)
    {
        // Clear separation between runs
        LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "=================================================");
        time_t roundStartTime = getUpTime();

        std::string data_temp = "Round #" + std::to_string(++countRounds) + " started";
        LogFile.WriteToFile(ESP_LOG_INFO, TAG, data_temp);

        fr_start = esp_timer_get_time();

        if (flowisrunning)
        {
            ESP_LOGD(TAG, "Autoflow: doFlow is already running!");
        }
        else
        {
            flowisrunning = true;
            doflow();
            LogFile.RemoveOldLogFile();
            LogFile.RemoveOldDataLog();
        }

        // Round finished -> Logfile
        LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Round #" + std::to_string(countRounds) + " completed (" + std::to_string(getUpTime() - roundStartTime) + " seconds)");

        // CPU Temp -> Logfile
        LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "CPU Temperature: " + std::to_string((int)temperatureRead()) + "°C");

        // WIFI Signal Strength (RSSI) -> Logfile
        LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "WIFI Signal (RSSI): " + std::to_string(get_WIFI_RSSI()) + "dBm");

        // Check if time is synchronized (if NTP is configured)
        if (getUseNtp() && !getTimeIsSet())
        {
            LogFile.WriteToFile(ESP_LOG_WARN, TAG, "Time server is configured, but time is not yet set!");
            setStatusLED(TIME_CHECK, 1, false);
        }

        if (network_config.connection_type == NETWORK_CONNECTION_WIFI_STA)
        {
            if (getWIFIisConnected())
            {
#if (defined WLAN_USE_MESH_ROAMING && defined WLAN_USE_MESH_ROAMING_ACTIVATE_CLIENT_TRIGGERED_QUERIES)
                wifiRoamingQuery();
#endif

#ifdef WLAN_USE_ROAMING_BY_SCANNING
                // Scan channels and check if an AP with better RSSI is available, then disconnect and try to reconnect to AP with better RSSI
                // NOTE: Keep this direct before the following task delay, because scan is done in blocking mode and this takes ca. 1,5 - 2s.
                wifiRoamByScanning();
#endif
            }
        }

        fr_delta_ms = (esp_timer_get_time() - fr_start) / 1000;
        if (auto_interval > fr_delta_ms)
        {
            if (deep_sleep_state && flowctrl.EspDeepSleepEnabled)
            {
                const TickType_t xDelay = (10000 / portTICK_PERIOD_MS);
                vTaskDelay(xDelay);

                LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "Deep sleep for " + std::to_string(auto_interval - (long)fr_delta_ms - (long)xDelay) + "ms");
                esp_sleep_enable_timer_wakeup((auto_interval - (long)fr_delta_ms - (long)xDelay) * 1000);
#if CONFIG_IDF_TARGET_ESP32
                // Isolate GPIO12 pin from external circuits. This is needed for modules
                // which have an external pull-up resistor on GPIO12 (such as ESP32-WROVER)
                // to minimize current consumption.
                rtc_gpio_isolate(GPIO_NUM_12);
                rtc_gpio_isolate(GPIO_NUM_4);
#endif

#if (defined(BOARD_ESP32_S3_ETH_V1) || defined(BOARD_ESP32_S3_ETH_V2))
                gpio_set_level(PER_ENABLE, 0);
#endif
                esp_deep_sleep_start();
            }
            else
            {
                deep_sleep_state = true;
                const TickType_t xDelay = (auto_interval - (long)fr_delta_ms) / portTICK_PERIOD_MS;
                LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "Autoflow: sleep for: " + std::to_string((long)xDelay) + "ms");
                vTaskDelay(xDelay);
            }
        }
    }

    while (1)
    {
        // Keep flow task running to handle necessary sub tasks like reboot handler, etc..
        vTaskDelay(2000 / portTICK_PERIOD_MS);
    }

    // Delete this task if it exits from the loop above
    vTaskDelete(NULL);
    xHandletask_autodoFlow = NULL;

    ESP_LOGD(TAG, "task_autodoFlow: end");
}

void InitializeFlowTask(void)
{
    BaseType_t xReturned;

    ESP_LOGD(TAG, "getESPHeapInfo: %s", getESPHeapInfo().c_str());

    uint32_t stackSize = 16 * 1024;
    xReturned = xTaskCreatePinnedToCore(&task_autodoFlow, "task_autodoFlow", stackSize, NULL, tskIDLE_PRIORITY + 2, &xHandletask_autodoFlow, 0);

    if (xReturned != pdPASS)
    {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Creation task_autodoFlow failed. Requested stack size:" + std::to_string(stackSize));
        LogFile.WriteHeapInfo("Creation task_autodoFlow failed");
    }

    ESP_LOGD(TAG, "getESPHeapInfo: %s", getESPHeapInfo().c_str());
}

static esp_err_t stay_online_handler(httpd_req_t *req)
{
    set_deep_sleep_state(false);

    /* Respond with an empty chunk to signal HTTP response completion */
    httpd_resp_send_chunk(req, NULL, 0);

    return ESP_OK;
}

static esp_err_t system_init_handler(httpd_req_t *req)
{
    set_deep_sleep_state(false);

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    const char *resp_str = "Init started<br>";
    httpd_resp_send(req, resp_str, HTTPD_RESP_USE_STRLEN);

    system_init();

    resp_str = "Init done<br>";
    httpd_resp_send(req, resp_str, HTTPD_RESP_USE_STRLEN);
    // Respond with an empty chunk to signal HTTP response completion
    httpd_resp_send_chunk(req, NULL, 0);

    return ESP_OK;
}

static esp_err_t flow_start_handler(httpd_req_t *req)
{
    set_deep_sleep_state(false);

    ESP_LOGD(TAG, "flow_start_handler uri: %s", req->uri);
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    if (autostartIsEnabled)
    {
        // Delay will be aborted if task is in blocked (waiting) state.
        // If task is already running, no action
        xTaskAbortDelay(xHandletask_autodoFlow);

        LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "Flow start triggered by REST API /flow_start");
        const char *resp_str = "The flow is going to be started immediately or is already running";
        httpd_resp_send(req, resp_str, HTTPD_RESP_USE_STRLEN);
        // Respond with an empty chunk to signal HTTP response completion
        httpd_resp_send_chunk(req, NULL, 0);
    }
    else
    {
        LogFile.WriteToFile(ESP_LOG_WARN, TAG, "Flow start triggered by REST API, but flow is not active!");
        httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Flow start triggered by REST API, but flow is not active");
        // Respond with an empty chunk to signal HTTP response completion
        httpd_resp_send_chunk(req, NULL, 0);
    }

    return ESP_OK;
}

static esp_err_t flow_status_handler(httpd_req_t *req)
{
    set_deep_sleep_state(false);

    const char *resp_str;
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    if (bTaskAutoFlowCreated)
    {
        std::string *temp_bufer = flowctrl.getActStatusWithTime();
        resp_str = temp_bufer->c_str();
        httpd_resp_send(req, resp_str, HTTPD_RESP_USE_STRLEN);
    }
    else
    {
        resp_str = "Flow task not currently created";
        httpd_resp_send(req, resp_str, HTTPD_RESP_USE_STRLEN);
    }

    // Respond with an empty chunk to signal HTTP response completion
    httpd_resp_send_chunk(req, NULL, 0);

    return ESP_OK;
}

static esp_err_t value_handler(httpd_req_t *req)
{
    set_deep_sleep_state(false);

    if (bTaskAutoFlowCreated)
    {
        char _query[100];
        char _value[10];

        if (httpd_req_get_url_query_str(req, _query, sizeof(_query)) == ESP_OK)
        {
            std::string _type = "value";
            int _intype = READOUT_TYPE_VALUE;

            if (httpd_query_key_value(_query, "type", _value, sizeof(_value)) == ESP_OK)
            {
                _type = std::string(_value);
            }

            httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

            httpd_resp_set_type(req, "text/plain");
            ESP_LOGD(TAG, "TYPE: %s", _type.c_str());

            if (_type == "prevalue")
            {
                _intype = READOUT_TYPE_PREVALUE;
            }
            else if (_type == "raw")
            {
                _intype = READOUT_TYPE_RAWVALUE;
            }
            else if (_type == "error")
            {
                _intype = READOUT_TYPE_ERROR;
            }

            std::string temp_bufer = flowctrl.getReadoutAll(_intype);
            ESP_LOGD(TAG, "temp_bufer: %s", temp_bufer.c_str());

            if (temp_bufer.length() > 0)
            {
                httpd_resp_send(req, temp_bufer.c_str(), temp_bufer.length());
            }

            // Respond with an empty chunk to signal HTTP response completion
            httpd_resp_send_chunk(req, NULL, 0);
            return ESP_OK;
        }
    }

    httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "REST API /value currently not available!");
    // Respond with an empty chunk to signal HTTP response completion
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_FAIL;
}

static esp_err_t set_PreValue_handler(httpd_req_t *req)
{
    set_deep_sleep_state(false);

    // Default usage message when handler gets called without any parameter
    const std::string RESTUsageInfo = "00: Handler usage:<br>"
                                      "- To retrieve actual PreValue, please provide only a numbersname, e.g. /setPreValue?numbers=main<br>"
                                      "- To set PreValue to a new value, please provide a numbersname and a value, e.g. /setPreValue?numbers=main&value=1234.5678<br>"
                                      "NOTE:<br>"
                                      "value >= 0.0: Set PreValue to provided value<br>"
                                      "value <  0.0: Set PreValue to actual RAW value (as long RAW value is a valid number, without N)";

    // Default return error message when no return is programmed
    std::string sReturnMessage = "E90: Uninitialized";

    char _query[100];
    char _numbersname[50] = "default";
    char _value[20] = "";

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    if (httpd_req_get_url_query_str(req, _query, sizeof(_query)) == ESP_OK)
    {
        if (httpd_query_key_value(_query, "numbers", _numbersname, sizeof(_numbersname)) != ESP_OK)
        {
            // If request is incomplete
            sReturnMessage = "E91: Query parameter incomplete or not valid!<br> "
                             "Call /setPreValue to show REST API usage info and/or check documentation";

            httpd_resp_send(req, sReturnMessage.c_str(), sReturnMessage.length());
            // Respond with an empty chunk to signal HTTP response completion
            httpd_resp_send_chunk(req, NULL, 0);
            return ESP_FAIL;
        }

        if (httpd_query_key_value(_query, "value", _value, sizeof(_value)) == ESP_OK)
        {
            ESP_LOGD(TAG, "Value: %s", _value);
        }
    }
    else
    {
        // if no parameter is provided, print handler usage
        httpd_resp_send(req, RESTUsageInfo.c_str(), RESTUsageInfo.length());
        // Respond with an empty chunk to signal HTTP response completion
        httpd_resp_send_chunk(req, NULL, 0);
        return ESP_OK;
    }

    if (strlen(_value) == 0)
    {
        // If no value is povided --> return actual PreValue
        sReturnMessage = flowctrl.GetPrevalue(std::string(_numbersname));
        if (sReturnMessage.empty())
        {
            sReturnMessage = "E92: Numbers name not found";
            httpd_resp_send(req, sReturnMessage.c_str(), sReturnMessage.length());
            // Respond with an empty chunk to signal HTTP response completion
            httpd_resp_send_chunk(req, NULL, 0);
            return ESP_FAIL;
        }
    }
    else
    {
        // New value is positive: Set PreValue to provided value and return value
        // New value is negative and actual RAW value is a valid number: Set PreValue to RAW value and return value
        LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "REST API handler_prevalue called: numbersname: " + std::string(_numbersname) + ", value: " + std::string(_value));

        if (!flowctrl.UpdatePrevalue(_value, _numbersname, true))
        {
            sReturnMessage = "E93: Update request rejected. Please check device logs for more details";
            httpd_resp_send(req, sReturnMessage.c_str(), sReturnMessage.length());
            // Respond with an empty chunk to signal HTTP response completion
            httpd_resp_send_chunk(req, NULL, 0);
            return ESP_FAIL;
        }

        sReturnMessage = flowctrl.GetPrevalue(std::string(_numbersname));
        if (sReturnMessage.empty())
        {
            sReturnMessage = "E94: Numbers name not found";
            httpd_resp_send(req, sReturnMessage.c_str(), sReturnMessage.length());
            // Respond with an empty chunk to signal HTTP response completion
            httpd_resp_send_chunk(req, NULL, 0);
            return ESP_FAIL;
        }
    }

    httpd_resp_send(req, sReturnMessage.c_str(), sReturnMessage.length());
    // Respond with an empty chunk to signal HTTP response completion
    httpd_resp_send_chunk(req, NULL, 0);

    return ESP_OK;
}

static esp_err_t editflow_handler(httpd_req_t *req)
{
    set_deep_sleep_state(false);

    ESP_LOGD(TAG, "editflow_handler uri: %s", req->uri);

    char _query[512];
    char _valuechar[30];

    if (httpd_req_get_url_query_str(req, _query, sizeof(_query)) == ESP_OK)
    {
        if (httpd_query_key_value(_query, "task", _valuechar, sizeof(_valuechar)) == ESP_OK)
        {
            std::string _task = std::string(toUpper(_valuechar));

            // wird beim Erstellen eines neuen Referenzbildes aufgerufen
            std::string *sys_status = flowctrl.getActStatus();

            if (_task.compare("NAMENUMBERS") == 0)
            {
                ESP_LOGD(TAG, "Get NUMBER list");
                get_number_list_handler(req);
            }
            else if (_task.compare("DATA") == 0)
            {
                ESP_LOGD(TAG, "Get data list");
                get_data_list_handler(req);
            }
            else if (_task.compare("TFLITE") == 0)
            {
                ESP_LOGD(TAG, "Get tflite list");
                get_tflite_list_handler(req);
            }
            else if (_task.compare("CONFIG") == 0)
            {
                ESP_LOGD(TAG, "Get config list");
                get_config_list_handler(req);
            }

            else if (_task.compare("COPY") == 0)
            {
                std::string temp_in, temp_out, temp_bufer;

                httpd_query_key_value(_query, "in", _valuechar, sizeof(_valuechar));
                temp_in = std::string(_valuechar);
                httpd_query_key_value(_query, "out", _valuechar, sizeof(_valuechar));
                temp_out = std::string(_valuechar);

                temp_in = "/sdcard" + temp_in;
                temp_out = "/sdcard" + temp_out;

                CopyFile(temp_in, temp_out);
                temp_bufer = "Copy Done";
                httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
                httpd_resp_send(req, temp_bufer.c_str(), temp_bufer.length());
            }

            else if (_task.compare("CUTREF") == 0)
            {
                std::string temp_in, temp_out, temp_bufer;
                int x = 0, y = 0, dx = 20, dy = 20;
                bool enhance = false;

                httpd_query_key_value(_query, "in", _valuechar, sizeof(_valuechar));
                temp_in = std::string(_valuechar);

                httpd_query_key_value(_query, "out", _valuechar, sizeof(_valuechar));
                temp_out = std::string(_valuechar);

                httpd_query_key_value(_query, "x", _valuechar, sizeof(_valuechar));
                std::string temp_x = std::string(_valuechar);
                if (isStringNumeric(temp_x))
                {
                    x = std::stoi(temp_x);
                }

                httpd_query_key_value(_query, "y", _valuechar, sizeof(_valuechar));
                std::string temp_y = std::string(_valuechar);
                if (isStringNumeric(temp_y))
                {
                    y = std::stoi(temp_y);
                }

                httpd_query_key_value(_query, "dx", _valuechar, sizeof(_valuechar));
                std::string temp_dx = std::string(_valuechar);
                if (isStringNumeric(temp_dx))
                {
                    dx = std::stoi(temp_dx);
                }

                httpd_query_key_value(_query, "dy", _valuechar, sizeof(_valuechar));
                std::string temp_dy = std::string(_valuechar);
                if (isStringNumeric(temp_dy))
                {
                    dy = std::stoi(temp_dy);
                }

                if (httpd_query_key_value(_query, "enhance", _valuechar, sizeof(_valuechar)) == ESP_OK)
                {
                    std::string temp_enhance = std::string(_valuechar);

                    if (temp_enhance.compare("true") == 0)
                    {
                        enhance = true;
                    }
                }

                temp_in = "/sdcard" + temp_in;
                temp_out = "/sdcard" + temp_out;

                std::string temp_out2 = temp_out.substr(0, temp_out.length() - 4) + "_org.jpg";

                if ((flowctrl.SetupModeActive || (*flowctrl.getActStatus() == std::string("Flow finished"))) && psram_init_shared_memory_for_take_image_step())
                {
                    LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Taking image for Alignment Mark Update...");

                    CAlignAndCutImage *caic = new CAlignAndCutImage("cutref", temp_in);
                    caic->CutAndSave(temp_out2, x, y, dx, dy);
                    delete caic;

                    CImageBasis *cim = new CImageBasis("cutref", temp_out2);

                    if (enhance)
                    {
                        cim->setImageContrast(90);
                    }

                    cim->SaveToFile(temp_out);
                    delete cim;

                    psram_deinit_shared_memory_for_take_image_step();
                    temp_bufer = "CutImage Done";
                }
                else
                {
                    LogFile.WriteToFile(ESP_LOG_WARN, TAG, std::string("Taking image for Alignment Mark not possible while device") + " is busy with a round (Current State: '" + *flowctrl.getActStatus() + "')!");
                    temp_bufer = "Device Busy";
                }

                httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
                httpd_resp_send(req, temp_bufer.c_str(), temp_bufer.length());
            }

            else if (_task.compare("PW_DECRYPT") == 0)
            {
                std::string temp_bufer = "";
                std::string temp_host = "";

                if (httpd_query_key_value(_query, "host", _valuechar, sizeof(_valuechar)) == ESP_OK)
                {
                    temp_host = std::string(_valuechar);
                }

                if (httpd_query_key_value(_query, "config_decrypt", _valuechar, sizeof(_valuechar)) == ESP_OK)
                {
                    std::string temp_config_decrypt = std::string(_valuechar);
                    if (alphanumericToBoolean(temp_config_decrypt))
                    {
                        if (EncryptDecryptPwOnSD(false, CONFIG_FILE) == ESP_OK)
                        {
                            temp_bufer = "decrypted";
                        }
                        else
                        {
                            temp_bufer = "not_decrypted";
                        }
                    }
                }

                else if (httpd_query_key_value(_query, "wifi_decrypt", _valuechar, sizeof(_valuechar)) == ESP_OK)
                {
                    std::string temp_wifi_decrypt = std::string(_valuechar);
                    if (alphanumericToBoolean(temp_wifi_decrypt))
                    {
                        if (EncryptDecryptPwOnSD(false, NETWORK_CONFIG_FILE) == ESP_OK)
                        {
                            temp_bufer = "decrypted";
                        }
                        else
                        {
                            temp_bufer = "not_decrypted";
                        }
                    }
                }

                ESP_LOGD(TAG, "Passwords decrypt: %s", temp_bufer.c_str());
                httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
                httpd_resp_send(req, temp_bufer.c_str(), temp_bufer.length());
            }

            else if (_task.compare("WIFI_CONFIG") == 0)
            {
                network_config.connection_type_temp = network_config.connection_type;
                network_config.ssid_temp = network_config.ssid;
                network_config.password_temp = network_config.password;
                network_config.hostname_temp = network_config.hostname;
                network_config.fix_ipaddress_used_temp = network_config.fix_ipaddress_used;
                network_config.ipaddress_temp = network_config.ipaddress;
                network_config.gateway_temp = network_config.gateway;
                network_config.netmask_temp = network_config.netmask;
                network_config.dns_temp = network_config.dns;
                network_config.rssi_threshold_temp = network_config.rssi_threshold;
                network_config.http_auth_temp = network_config.http_auth;
                network_config.http_username_temp = network_config.http_username;
                network_config.http_password_temp = network_config.http_password;

                std::string temp_host = "";

                if (httpd_query_key_value(_query, "host", _valuechar, sizeof(_valuechar)) == ESP_OK)
                {
                    temp_host = std::string(_valuechar);
                }

                if (httpd_query_key_value(_query, "contype", _valuechar, sizeof(_valuechar)) == ESP_OK)
                {
                    network_config.connection_type_temp = std::string(_valuechar);
                }

                if (httpd_query_key_value(_query, "hostname", _valuechar, sizeof(_valuechar)) == ESP_OK)
                {
                    network_config.hostname_temp = UrlDecode(_valuechar);
                }

                if (httpd_query_key_value(_query, "ssid", _valuechar, sizeof(_valuechar)) == ESP_OK)
                {
                    network_config.ssid_temp = UrlDecode(_valuechar);
                }

                if (httpd_query_key_value(_query, "password", _valuechar, sizeof(_valuechar)) == ESP_OK)
                {
                    network_config.password_temp = EncryptDecryptString(UrlDecode(_valuechar));
                }

                if (httpd_query_key_value(_query, "fixip", _valuechar, sizeof(_valuechar)) == ESP_OK)
                {
                    std::string temp_fixip = std::string(_valuechar);
                    network_config.fix_ipaddress_used_temp = alphanumericToBoolean(temp_fixip);
                }

                if (httpd_query_key_value(_query, "ip", _valuechar, sizeof(_valuechar)) == ESP_OK)
                {
                    network_config.ipaddress_temp = std::string(_valuechar);
                }

                if (httpd_query_key_value(_query, "gateway", _valuechar, sizeof(_valuechar)) == ESP_OK)
                {
                    network_config.gateway_temp = std::string(_valuechar);
                }

                if (httpd_query_key_value(_query, "netmask", _valuechar, sizeof(_valuechar)) == ESP_OK)
                {
                    network_config.netmask_temp = std::string(_valuechar);
                }

                if (httpd_query_key_value(_query, "dns", _valuechar, sizeof(_valuechar)) == ESP_OK)
                {
                    network_config.dns_temp = std::string(_valuechar);
                }

                if (httpd_query_key_value(_query, "rssi", _valuechar, sizeof(_valuechar)) == ESP_OK)
                {
                    std::string temp_rssi = std::string(_valuechar);
                    if (isStringNumeric(temp_rssi))
                    {
                        network_config.rssi_threshold_temp = std::stoi(temp_rssi);
                    }
                }

                if (httpd_query_key_value(_query, "auth", _valuechar, sizeof(_valuechar)) == ESP_OK)
                {
                    std::string temp_auth = std::string(_valuechar);
                    network_config.http_auth_temp = alphanumericToBoolean(temp_auth);
                }

                if (httpd_query_key_value(_query, "authuser", _valuechar, sizeof(_valuechar)) == ESP_OK)
                {
                    network_config.http_username_temp = UrlDecode(_valuechar);
                }

                if (httpd_query_key_value(_query, "authpw", _valuechar, sizeof(_valuechar)) == ESP_OK)
                {
                    network_config.http_password_temp = EncryptDecryptString(UrlDecode(_valuechar));
                }

                if (ChangeNetworkConfig(NETWORK_CONFIG_FILE) == ESP_OK)
                {
                    ESP_LOGD(TAG, "Wifi Settings set");
                    std::string temp_bufer = "WifiSettingsSet";
                    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
                    httpd_resp_send(req, temp_bufer.c_str(), temp_bufer.length());
                }
                else
                {
                    ESP_LOGD(TAG, "Wifi Settings not set");
                    std::string temp_bufer = "WifiSettingsNotSet";
                    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
                    httpd_resp_send(req, temp_bufer.c_str(), temp_bufer.length());
                }
            }

            else if (_task.compare("SYSTEM_CONFIG") == 0)
            {
                if ((sys_status->c_str() != std::string("Take Image")) && (sys_status->c_str() != std::string("Aligning")))
                {
                    std::string temp_host = "";

                    if (httpd_query_key_value(_query, "host", _valuechar, sizeof(_valuechar)) == ESP_OK)
                    {
                        temp_host = std::string(_valuechar);
                    }

                    if (httpd_query_key_value(_query, "tzone", _valuechar, sizeof(_valuechar)) == ESP_OK)
                    {
                        std::string temp_tzone = std::string(_valuechar);
                    }

                    if (httpd_query_key_value(_query, "tserver", _valuechar, sizeof(_valuechar)) == ESP_OK)
                    {
                        std::string temp_tserver = std::string(_valuechar);
                    }

                    if (httpd_query_key_value(_query, "cpufreq", _valuechar, sizeof(_valuechar)) == ESP_OK)
                    {
                        std::string temp_cpufreq = std::string(_valuechar);
                        if (isStringNumeric(temp_cpufreq))
                        {
                            int system_cpufreq = clipInt(std::stoi(temp_cpufreq), 240, 160);
                        }
                    }

                    if (httpd_query_key_value(_query, "setup", _valuechar, sizeof(_valuechar)) == ESP_OK)
                    {
                        std::string temp_setup = std::string(_valuechar);
                        bool system_setup = alphanumericToBoolean(temp_setup);
                    }

                    ESP_LOGD(TAG, "System Settings set");
                    std::string temp_bufer = "SystemSettingsSet";
                    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
                    httpd_resp_send(req, temp_bufer.c_str(), temp_bufer.length());
                }
                else
                {
                    std::string temp_bufer = "DeviceIsBusy";
                    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
                    httpd_resp_send(req, temp_bufer.c_str(), temp_bufer.length());
                }
            }
            else if (_task.compare("TEST_ALIGN") == 0)
            {
                if ((sys_status->c_str() != std::string("Take Image")) && (sys_status->c_str() != std::string("Aligning")))
                {
                    std::string temp_host = "";

                    if (httpd_query_key_value(_query, "host", _valuechar, sizeof(_valuechar)) == ESP_OK)
                    {
                        temp_host = std::string(_valuechar);
                    }

                    std::string temp_bufer = flowctrl.doSingleStep("[Alignment]", temp_host);
                    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
                    httpd_resp_send(req, temp_bufer.c_str(), temp_bufer.length());
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

static esp_err_t recognition_handler(httpd_req_t *req)
{
    set_deep_sleep_state(false);

    if (bTaskAutoFlowCreated)
    {
        bool _rawValue = false;
        bool _noerror = false;

        ESP_LOGD(TAG, "handler water counter uri: %s", req->uri);

        char _query[100];
        char _value[10];

        if (httpd_req_get_url_query_str(req, _query, sizeof(_query)) == ESP_OK)
        {
            if (httpd_query_key_value(_query, "rawvalue", _value, sizeof(_value)) == ESP_OK)
            {
                _rawValue = true;
            }

            if (httpd_query_key_value(_query, "noerror", _value, sizeof(_value)) == ESP_OK)
            {
                _noerror = true;
            }
        }

        std::string *status = flowctrl.getActStatus();
        std::string query = std::string(_query);

        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

        std::string txt;
        txt = "<body style=\"font-family: arial\">";

        if ((countRounds <= 1) && (*status != std::string("Flow finished")))
        {
            // First round not completed currently
            txt += "<h3>Please wait for the first round to complete!</h3><h3>Current state: " + *status + "</h3>\n";
        }
        else
        {
            txt += "<h3>Value</h3>";
        }
        httpd_resp_sendstr_chunk(req, txt.c_str());

        std::string temp_bufer2 = flowctrl.getReadout(_rawValue, _noerror, 0);
        if (temp_bufer2.length() > 0)
        {
            httpd_resp_sendstr_chunk(req, temp_bufer2.c_str());
        }

        std::string temp_txt, temp_bufer3;

        if ((countRounds <= 1) && (*status != std::string("Flow finished")))
        {
            // First round not completed currently
            // Nothing to do
        }
        else
        {
            /* Digit ROIs */
            temp_txt = "<body style=\"font-family: arial\">";
            temp_txt += "<hr><h3>Recognized Digit ROIs (previous round)</h3>\n";
            temp_txt += "<table style=\"border-spacing: 5px\"><tr style=\"text-align: center; vertical-align: top;\">\n";

            std::vector<HTMLInfo *> htmlinfodig;
            htmlinfodig = flowctrl.GetAllDigit();

            for (int i = 0; i < htmlinfodig.size(); ++i)
            {
                if (flowctrl.GetTypeDigit() == Digit)
                {
                    // Numbers greater than 10 and less than 0 indicate NaN, since a Roi can only have values ​​from 0 to 9.
                    if ((htmlinfodig[i]->val >= 10) || (htmlinfodig[i]->val < 0))
                    {
                        temp_bufer3 = "NaN";
                    }
                    else
                    {
                        temp_bufer3 = std::to_string((int)htmlinfodig[i]->val);
                    }

                    temp_txt += "<td style=\"width: 100px\"><h4>" + temp_bufer3 + "</h4><p><img src=\"/img_tmp/" + htmlinfodig[i]->filename + "\"></p></td>\n";
                }
                else
                {
                    std::stringstream stream;
                    stream << std::fixed << std::setprecision(1) << htmlinfodig[i]->val;
                    temp_bufer3 = stream.str();

                    // Numbers greater than 10 and less than 0 indicate NaN, since a Roi can only have values ​​from 0 to 9.
                    if ((std::stod(temp_bufer3) >= 10) || (std::stod(temp_bufer3) < 0))
                    {
                        temp_bufer3 = "NaN";
                    }

                    temp_txt += "<td style=\"width: 100px\"><h4>" + temp_bufer3 + "</h4><p><img src=\"/img_tmp/" + htmlinfodig[i]->filename + "\"></p></td>\n";
                }
                delete htmlinfodig[i];
            }

            htmlinfodig.clear();

            temp_txt += "</tr></table>\n";
            httpd_resp_sendstr_chunk(req, temp_txt.c_str());

            /* Analog ROIs */
            temp_txt = "<hr><h3>Recognized Analog ROIs (previous round)</h3>\n";
            temp_txt += "<table style=\"border-spacing: 5px\"><tr style=\"text-align: center; vertical-align: top;\">\n";

            std::vector<HTMLInfo *> htmlinfoana;
            htmlinfoana = flowctrl.GetAllAnalog();

            for (int i = 0; i < htmlinfoana.size(); ++i)
            {
                std::stringstream stream;
                stream << std::fixed << std::setprecision(1) << htmlinfoana[i]->val;
                temp_bufer3 = stream.str();

                // Numbers greater than 10 and less than 0 indicate NaN, since a Roi can only have values ​​from 0 to 9.
                if ((std::stod(temp_bufer3) >= 10) || (std::stod(temp_bufer3) < 0))
                {
                    temp_bufer3 = "NaN";
                }

                temp_txt += "<td style=\"width: 150px;\"><h4>" + temp_bufer3 + "</h4><p><img src=\"/img_tmp/" + htmlinfoana[i]->filename + "\"></p></td>\n";
                delete htmlinfoana[i];
            }

            htmlinfoana.clear();

            temp_txt += "</tr>\n</table>\n";
            httpd_resp_sendstr_chunk(req, temp_txt.c_str());

            /* Full Image
             * Only show it after the image got taken */
            temp_txt = "<hr><h3>Full Image (current round)</h3>\n";

            if ((*status == std::string("Initialization")) || (*status == std::string("Initialization (delayed)")) || (*status == std::string("Take Image")))
            {
                temp_txt += "<p>Current state: " + *status + "</p>\n";
            }
            else
            {
                temp_txt += "<img src=\"/img_tmp/alg_roi.jpg\">\n";
            }
            httpd_resp_sendstr_chunk(req, temp_txt.c_str());
        }

        // Respond with an empty chunk to signal HTTP response completion
        httpd_resp_send_chunk(req, NULL, 0);
    }
    else
    {
        httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "REST API /value currently not available!");
        // Respond with an empty chunk to signal HTTP response completion
        httpd_resp_send_chunk(req, NULL, 0);
        return ESP_FAIL;
    }

    return ESP_OK;
}

static esp_err_t img_tmp_handler(httpd_req_t *req)
{
    ESP_LOGD(TAG, "uri: %s", req->uri);

    char filepath[FILE_PATH_MAX];
    const char *filename = get_path_from_uri(filepath, ((rest_server_context_t *)req->user_ctx)->base_path, req->uri + sizeof("/img_tmp/") - 1, sizeof(filepath));
    ESP_LOGD(TAG, "1 uri: %s, filename: %s, filepath: %s", req->uri, filename, filepath);

    std::string filetosend(((rest_server_context_t *)req->user_ctx)->base_path);
    filetosend = filetosend + "/img_tmp/" + std::string(filename);
    ESP_LOGD(TAG, "File to upload: %s", filetosend.c_str());

    esp_err_t res = send_file(req, filetosend);
    if (res != ESP_OK)
    {
        return res;
    }

    /* Respond with an empty chunk to signal HTTP response completion */
    httpd_resp_send_chunk(req, NULL, 0);

    return ESP_OK;
}

static esp_err_t img_tmp_virtual_handler(httpd_req_t *req)
{
    set_deep_sleep_state(false);

    ESP_LOGD(TAG, "uri: %s", req->uri);

    char filepath[FILE_PATH_MAX];
    const char *filename = get_path_from_uri(filepath, ((rest_server_context_t *)req->user_ctx)->base_path, req->uri + sizeof("/img_tmp/") - 1, sizeof(filepath));
    ESP_LOGD(TAG, "1 uri: %s, filename: %s, filepath: %s", req->uri, filename, filepath);

    ESP_LOGD(TAG, "File to upload: %s", filename);

    // Serve raw.jpg
    if (std::string(filename) == "raw.jpg")
    {
        return GetRawJPG(req);
    }

    // Serve alg.jpg, alg_roi.jpg or digit and analog ROIs
    if (ESP_OK == GetJPG(std::string(filename), req))
    {
        return ESP_OK;
    }

    // File was not served already --> serve with img_tmp_handler
    return img_tmp_handler(req);
}

/**
 * Generates a http response containing the OpenMetrics (https://openmetrics.io/) text wire format
 * according to https://github.com/OpenObservability/OpenMetrics/blob/main/specification/OpenMetrics.md#text-format.
 *
 * A MetricFamily with a Metric for each Sequence is provided. If no valid value is available, the metric is not provided.
 * MetricPoints are provided without a timestamp. Additional metrics with some device information is also provided.
 *
 * The metric name prefix is 'ai_on_the_edge_device_'.
 *
 * example configuration for Prometheus (`prometheus.yml`):
 *
 *    - job_name: watermeter
 *      static_configs:
 *        - targets: ['watermeter.fritz.box']
 *
 */
esp_err_t openmetrics_handler(httpd_req_t *req)
{
    set_deep_sleep_state(false);

    ESP_LOGD(TAG, "openmetrics_handler uri: %s", req->uri);

    if (bTaskAutoFlowCreated)
    {
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

        // application/openmetrics-text is not yet supported by prometheus so we use text/plain for now
        httpd_resp_set_type(req, "text/plain");

        const string metricNamePrefix = "ai_on_the_edge_device";

        // get current measurement (flow)
        string response = createSequenceMetrics(metricNamePrefix, flowctrl.getNumbers());

        // CPU Temperature
        response += createMetric(metricNamePrefix + "_cpu_temperature_celsius", "current cpu temperature in celsius", "gauge", std::to_string((int)temperatureRead()));

        // WiFi signal strength
        response += createMetric(metricNamePrefix + "_rssi_dbm", "current WiFi signal strength in dBm", "gauge", std::to_string(get_WIFI_RSSI()));

        // memory info
        response += createMetric(metricNamePrefix + "_memory_heap_free_bytes", "available heap memory", "gauge", std::to_string(getESPHeapSize()));

        // device uptime
        response += createMetric(metricNamePrefix + "_uptime_seconds", "device uptime in seconds", "gauge", std::to_string((long)getUpTime()));

        // data aquisition round
        response += createMetric(metricNamePrefix + "_rounds_total", "data aquisition rounds since device startup", "counter", std::to_string(countRounds));

        // the response always contains at least the metadata (HELP, TYPE) for the MetricFamily so no length check is needed
        httpd_resp_send(req, response.c_str(), response.length());
    }
    else
    {
        httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Flow not (yet) started: REST API /metrics not yet available!");
        /* Respond with an empty chunk to signal HTTP response completion */
        httpd_resp_send_chunk(req, NULL, 0);
        return ESP_ERR_NOT_FOUND;
    }

    return ESP_OK;
}

esp_err_t register_server_main_flow_uri(httpd_handle_t my_server)
{
    esp_err_t ret = ESP_OK;

    httpd_uri_t stay_online = {
        .uri = "/stay_online",
        .method = HTTP_GET,
        .handler = APPLY_BASIC_AUTH_FILTER(stay_online_handler),
        .user_ctx = rest_context // Pass server data as context
    };
    ret |= httpd_register_uri_handler(my_server, &stay_online);

    httpd_uri_t system_init = {
        .uri = "/system_init",
        .method = HTTP_GET,
        .handler = APPLY_BASIC_AUTH_FILTER(system_init_handler),
        .user_ctx = rest_context // Pass server data as context
    };
    ret |= httpd_register_uri_handler(my_server, &system_init);

    httpd_uri_t flow_start = {
        .uri = "/flow_start",
        .method = HTTP_GET,
        .handler = APPLY_BASIC_AUTH_FILTER(flow_start_handler),
        .user_ctx = rest_context // Pass server data as context
    };
    ret |= httpd_register_uri_handler(my_server, &flow_start);

    httpd_uri_t flow_status = {
        .uri = "/flow_status",
        .method = HTTP_GET,
        .handler = APPLY_BASIC_AUTH_FILTER(flow_status_handler),
        .user_ctx = rest_context // Pass server data as context
    };
    ret |= httpd_register_uri_handler(my_server, &flow_status);

    httpd_uri_t value = {
        .uri = "/value",
        .method = HTTP_GET,
        .handler = APPLY_BASIC_AUTH_FILTER(value_handler),
        .user_ctx = rest_context // Pass server data as context
    };
    ret |= httpd_register_uri_handler(my_server, &value);

    httpd_uri_t setPreValue = {
        .uri = "/setPreValue",
        .method = HTTP_GET,
        .handler = APPLY_BASIC_AUTH_FILTER(set_PreValue_handler),
        .user_ctx = rest_context // Pass server data as context
    };
    ret |= httpd_register_uri_handler(my_server, &setPreValue);

    httpd_uri_t editflow = {
        .uri = "/editflow",
        .method = HTTP_GET,
        .handler = APPLY_BASIC_AUTH_FILTER(editflow_handler),
        .user_ctx = rest_context // Pass server data as context
    };
    ret |= httpd_register_uri_handler(my_server, &editflow);

    httpd_uri_t recognition = {
        .uri = "/recognition",
        .method = HTTP_GET,
        .handler = APPLY_BASIC_AUTH_FILTER(recognition_handler),
        .user_ctx = rest_context // Pass server data as context
    };
    ret |= httpd_register_uri_handler(my_server, &recognition);

    httpd_uri_t img_tmp_get_handle = {
        .uri = "/img_tmp/*", // Match all URIs of type /path/to/file
        .method = HTTP_GET,
        .handler = APPLY_BASIC_AUTH_FILTER(img_tmp_virtual_handler),
        .user_ctx = rest_context // Pass server data as context
    };
    ret |= httpd_register_uri_handler(my_server, &img_tmp_get_handle);

    httpd_uri_t metrics = {
        .uri = "/metrics", // Match all URIs of type /path/to/file
        .method = HTTP_GET,
        .handler = APPLY_BASIC_AUTH_FILTER(openmetrics_handler),
        .user_ctx = rest_context // Pass server data as context
    };
    ret |= httpd_register_uri_handler(my_server, &metrics);

    return ret;
}
