/* Async Request Handlers HTTP Server Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include "server_main.h"
#include "server_config.h"

#include <sdkconfig.h>
#include <string>
#include <esp_wifi.h>
#include <netdb.h>
#include <esp_log.h>
#include <stdio.h>
#include <esp_chip_info.h>
#include <esp_vfs.h>
#include <esp_netif.h>
#include <esp_eth.h>
#include <esp_http_server.h>

#include "server_help.h"
#include "ClassLogFile.h"

#include "time_sntp.h"

#include "connect_wifi_ap.h"
#include "connect_wifi_sta.h"
#include "read_network_config.h"
#if (CONFIG_ETH_ENABLED && CONFIG_ETH_USE_SPI_ETHERNET && CONFIG_ETH_SPI_ETHERNET_W5500)
#include "connect_eth.h"
#endif

#include "../main/version.h"

#include "MainFlowControl.h"
#include "server_file.h"
#include "server_ota.h"
#include "server_camera.h"

#ifdef ENABLE_MQTT
#include "server_mqtt.h"
#endif // ENABLE_MQTT

#include "basic_auth.h"

#include "statusled.h"
#include "Helper.h"
#include "cJSON.h"

static const char *TAG = "MAIN SERVER";

httpd_handle_t my_httpd_server = NULL;
httpd_config_t my_httpd_config = HTTPD_DEFAULT_CONFIG();
std::string starttime_sta = "";

#ifdef CONFIG_HTTPD_WS_SUPPORT
/*
 * Structure holding server handle
 * and internal socket fd in order
 * to use out of request send
 */
struct async_resp_req
{
    httpd_handle_t hd;
    int fd;
    int request;
};

#define WS_SERVER_USE_PSRAM
#endif

esp_err_t Init_Network(void)
{
#ifdef HEAP_TRACING_MAIN_NETWORK
    ESP_ERROR_CHECK(heap_trace_start(HEAP_TRACE_LEAKS));
#endif

    esp_err_t ret = ESP_OK;
    TickType_t xDelay = 500 / portTICK_PERIOD_MS;

    // Read Network parameter and start it
    // ********************************************
    int iNetworkStatus = LoadNetworkConfigFromFile(NETWORK_CONFIG_FILE);

    // Network config available (0) or SSID/password not configured (-2)
    if (FileExists(CONFIG_FILE) && ((iNetworkStatus == 0) || (iNetworkStatus == -2)))
    {
        LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Network config loaded, init Network...");

        if (network_config.connection_type == NETWORK_CONNECTION_WIFI_AP_SETUP)
        {
            ret = wifi_init_ap();
            if (ret != ESP_OK)
            {
                LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Wifi AP init failed. Device init aborted!");
                setStatusLED(WLAN_INIT, 3, true);
                return ret;
            }

            vTaskDelay(xDelay);

            ret = Init_Server_Config();
            if (ret != ESP_OK)
            {
                LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Wifi AP Setup Server init failed. Device init aborted!");
                setStatusLED(WLAN_INIT, 3, true);
                return ret;
            }
        }
        else if (network_config.connection_type == NETWORK_CONNECTION_WIFI_AP)
        {
            ret = wifi_init_ap();
            if (ret != ESP_OK)
            {
                LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Wifi AP init failed. Device init aborted!");
                setStatusLED(WLAN_INIT, 3, true);
                return ret;
            }

            vTaskDelay(xDelay);

            ret = Init_Webserver();
            if (ret != ESP_OK)
            {
                LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Wifi AP Server init failed. Device init aborted!");
                setStatusLED(WLAN_INIT, 3, true);
                return ret;
            }
        }
        else if (network_config.connection_type == NETWORK_CONNECTION_WIFI_STA)
        {
            if (iNetworkStatus == -2)
            {
                network_config.connection_type = NETWORK_CONNECTION_WIFI_AP_SETUP;

                LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "SSID or password not configured!");
                setStatusLED(WLAN_INIT, 2, true);

                ret = wifi_init_ap();
                if (ret != ESP_OK)
                {
                    LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Wifi AP init failed. Device init aborted!");
                    setStatusLED(WLAN_INIT, 3, true);
                    return ret;
                }

                vTaskDelay(xDelay);

                ret = Init_Server_Config();
                if (ret != ESP_OK)
                {
                    LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Wifi AP Setup init failed. Device init aborted!");
                    setStatusLED(WLAN_INIT, 3, true);
                    return ret;
                }
            }
            else
            {
                ret = wifi_init_sta();
                if (ret != ESP_OK)
                {
                    LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Wifi STA init failed. Device init aborted!");
                    setStatusLED(WLAN_INIT, 3, true);
                    return ret;
                }

                vTaskDelay(xDelay);

                ret = Init_Webserver();
                if (ret != ESP_OK)
                {
                    LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Wifi STA Server init failed. Device init aborted!");
                    setStatusLED(WLAN_INIT, 3, true);
                    return ret;
                }
            }
        }
#if (CONFIG_ETH_ENABLED && CONFIG_ETH_USE_SPI_ETHERNET && CONFIG_ETH_SPI_ETHERNET_W5500)
        else if (network_config.connection_type == NETWORK_CONNECTION_ETH)
        {
            ret = eth_init_W5500();
            if (ret != ESP_OK)
            {
                LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Ethernet init failed. Device init aborted!");
                setStatusLED(WLAN_INIT, 3, true);
                return ret;
            }

            vTaskDelay(xDelay);

            ret = Init_Webserver();
            if (ret != ESP_OK)
            {
                LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Ethernet Server init failed. Device init aborted!");
                setStatusLED(WLAN_INIT, 3, true);
                return ret;
            }
        }
#endif // (CONFIG_ETH_ENABLED && CONFIG_ETH_USE_SPI_ETHERNET && CONFIG_ETH_SPI_ETHERNET_W5500)
        else if (network_config.connection_type == NETWORK_CONNECTION_DISCONNECT)
        {
        }
    }
    // network.ini not available (-1) and config.ini not available
    else
    {
        network_config.connection_type = NETWORK_CONNECTION_WIFI_AP_SETUP;

        ret = wifi_init_ap();
        if (ret != ESP_OK)
        {
            LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Wifi AP init failed. Device init aborted!");
            setStatusLED(WLAN_INIT, 3, true);
            return ret;
        }

        vTaskDelay(xDelay);

        ret = Init_Server_Config();
        if (ret != ESP_OK)
        {
            LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Wifi AP Server init failed. Device init aborted!");
            setStatusLED(WLAN_INIT, 3, true);
            return ret;
        }
    }

    init_basic_auth();

    ESP_LOGD(TAG, "main: sleep for: %ldms", (long)xDelay * CONFIG_FREERTOS_HZ / portTICK_PERIOD_MS);
    vTaskDelay(xDelay);

#ifdef HEAP_TRACING_MAIN_NETWORK
    ESP_ERROR_CHECK(heap_trace_stop());
    heap_trace_dump();
#endif

    return ret;
}

esp_err_t Init_Webserver(void)
{
    // Start webserver + register handler
    // ********************************************
    ESP_LOGD(TAG, "starting servers");

    esp_err_t ret = ESP_OK;

    ret = start_webserver();
    if (ret != ESP_OK)
    {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "start_webserver: Error: " + std::string(esp_err_to_name(ret)));
        return ret; // No way to continue without working WLAN!
    }

    ret = register_server_file_uri(SD_PARTITION_PATH, my_httpd_server);
    if (ret != ESP_OK)
    {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "register_server_file_uri: Error: " + std::string(esp_err_to_name(ret)));
        return ret; // No way to continue without working webserver!
    }

    ret = register_server_main_flow_uri(my_httpd_server);
    if (ret != ESP_OK)
    {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "register_server_main_flow_uri: Error: " + std::string(esp_err_to_name(ret)));
        return ret; // No way to continue without working webserver!
    }

    ret = register_server_camera_uri(my_httpd_server);
    if (ret != ESP_OK)
    {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "register_server_camera_uri: Error: " + std::string(esp_err_to_name(ret)));
        return ret; // No way to continue without working webserver!
    }

    ret = register_server_ota_uri(my_httpd_server);
    if (ret != ESP_OK)
    {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "register_ota_server_uri: Error: " + std::string(esp_err_to_name(ret)));
        return ret; // No way to continue without working webserver!
    }

#ifdef ENABLE_MQTT
    ret = register_server_mqtt_uri(my_httpd_server);
    if (ret != ESP_OK)
    {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "register_mqtt_server_uri: Error: " + std::string(esp_err_to_name(ret)));
        return ret; // No way to continue without working webserver!
    }
#endif // ENABLE_MQTT

    gpio_handler_create(my_httpd_server);

    ret = register_server_main_uri(my_httpd_server);
    if (ret != ESP_OK)
    {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "register_rest_server_uri: Error: " + std::string(esp_err_to_name(ret)));
        return ret; // No way to continue without working webserver!
    }

    return ret;
}

esp_err_t start_webserver(void)
{
    esp_err_t ret = ESP_OK;

    my_httpd_config.task_priority = tskIDLE_PRIORITY + 3; // previously -> 2022-12-11: tskIDLE_PRIORITY+1; 2021-09-24: tskIDLE_PRIORITY+5
    my_httpd_config.stack_size = 6 * 2048;                // previously -> 2023-01-02: 32768
    my_httpd_config.core_id = 1;                          // previously -> 2023-01-02: 0, 2022-12-11: tskNO_AFFINITY;
    my_httpd_config.server_port = 80;
    my_httpd_config.ctrl_port = 32768;
    my_httpd_config.max_open_sockets = 5;  // 20210921 --> previously 7
    my_httpd_config.max_uri_handlers = 42; // Make sure this fits all URI handlers. Memory usage in bytes: 6*max_uri_handlers
    my_httpd_config.max_resp_headers = 8;
    my_httpd_config.backlog_conn = 5;
    my_httpd_config.lru_purge_enable = true; // this cuts old connections if new ones are needed.
    my_httpd_config.recv_wait_timeout = 5;   // default: 5 20210924 --> previously 30
    my_httpd_config.send_wait_timeout = 5;   // default: 5 20210924 --> previously 30
    my_httpd_config.global_user_ctx = NULL;
    my_httpd_config.global_user_ctx_free_fn = NULL;
    my_httpd_config.global_transport_ctx = NULL;
    my_httpd_config.global_transport_ctx_free_fn = NULL;
    my_httpd_config.open_fn = NULL;
    my_httpd_config.close_fn = NULL;
    // my_httpd_config.uri_match_fn = NULL;
    my_httpd_config.uri_match_fn = httpd_uri_match_wildcard;

    starttime_sta = getCurrentTimeString("%Y%m%d-%H%M%S");

    // Start the httpd server
    ESP_LOGI(TAG, "Starting server on port: '%d'", my_httpd_config.server_port);
    ret = httpd_start(&my_httpd_server, &my_httpd_config);
    if (ret != ESP_OK)
    {
        if (ret == ESP_ERR_INVALID_ARG)
        {
            ESP_LOGE(TAG, "Error starting server : Null argument(s)!");
        }
        else if (ret == ESP_ERR_HTTPD_ALLOC_MEM)
        {
            ESP_LOGE(TAG, "Error starting server : Failed to allocate memory for instance!");
        }
        else if (ret == ESP_ERR_HTTPD_TASK)
        {
            ESP_LOGE(TAG, "Error starting server : Failed to launch server task!");
        }
        else
        {
            ESP_LOGE(TAG, "Error starting server : Unknown Error!");
        }

        return ret;
    }

    ESP_LOGI(TAG, "Start server successfully");
    return ret;
}

void stop_webserver(httpd_handle_t server)
{
    httpd_stop(server);
}

void disconnect_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    httpd_handle_t *server = (httpd_handle_t *)arg;
    if (*server)
    {
        ESP_LOGI(TAG, "Stopping webserver");
        stop_webserver(*server);
        *server = NULL;
    }
}

void connect_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    httpd_handle_t *server = (httpd_handle_t *)arg;
    if (*server == NULL)
    {
        ESP_LOGI(TAG, "Starting webserver");
        esp_err_t ret = start_webserver();
        if (ret == ESP_OK)
        {
            *server = my_httpd_server;
        }
    }
}

/* Send HTTP response with the contents of the requested file */
static esp_err_t main_server_handler(httpd_req_t *req)
{
    set_deep_sleep_state(false);

    char filepath[FILE_PATH_MAX];
    strlcpy(filepath, ((rest_server_context_t *)req->user_ctx)->base_path, sizeof(filepath));

    if (req->uri[strlen(req->uri) - 1] == '/')
    {
        strlcat(filepath, "/html/index.html", sizeof(filepath));
    }
    else
    {
        strlcat(filepath, "/html", sizeof(filepath));
        strlcat(filepath, req->uri, sizeof(filepath));

        int pos = std::string(filepath).find("?");
        if (pos > -1)
        {
            strlcpy(filepath, (std::string(filepath).substr(0, pos)).c_str(), sizeof(filepath));
        }
    }

    ESP_LOGD(TAG, "File requested: %s", filepath);

    if (std::string(filepath) == "/sdcard/html/index.html")
    {
        // Initialization failed with crritical errors!
        if (isSetSystemStatusFlag(SYSTEM_STATUS_PSRAM_BAD) ||
            isSetSystemStatusFlag(SYSTEM_STATUS_CAM_BAD) ||
            isSetSystemStatusFlag(SYSTEM_STATUS_SDCARD_CHECK_BAD) ||
            isSetSystemStatusFlag(SYSTEM_STATUS_FOLDER_CHECK_BAD))
        {
            LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "We have a critical error, not serving main page!");

            char buf[20];
            std::string message = "<h1>AI on the Edge Device</h1><b>We have one or more critical errors:</b><br>";

            for (int i = 0; i < 32; i++)
            {
                if (isSetSystemStatusFlag((SystemStatusFlag_t)(1 << i)))
                {
                    snprintf(buf, sizeof(buf), "0x%08X", 1 << i);
                    message += std::string(buf) + "<br>";
                }
            }

            message += "<br>Please check logs with log viewer and/or <a href=\"https://jomjol.github.io/AI-on-the-edge-device-docs/Error-Codes\" target=_blank>jomjol.github.io/AI-on-the-edge-device-docs/Error-Codes</a> for more information!";
            message += "<br><br><button onclick=\"window.location.href='/reboot';\">Reboot</button>";
            message += "&nbsp;<button onclick=\"window.open('/ota_page.html');\">OTA Update</button>";
            message += "&nbsp;<button onclick=\"window.open('/log.html');\">Log Viewer</button>";
            message += "&nbsp;<button onclick=\"window.open('/info.html');\">Show System Info</button>";
            httpd_resp_send(req, message.c_str(), message.length());
            return ESP_OK;
        }
        else if (isSetupModusActive())
        {
            ESP_LOGD(TAG, "System is in setup mode --> index.html --> setup.html");
            strlcpy(filepath, "/sdcard/html/setup.html", sizeof(filepath));
        }
    }

    ESP_LOGD(TAG, "File requested: %s", filepath);

    esp_err_t ret = send_file(req, std::string(filepath));

    /* Respond with an empty chunk to signal HTTP response completion */
    httpd_resp_send_chunk(req, NULL, 0);

    return ret;
}

/* Handler for getting system info */
static esp_err_t system_info_get_handler(httpd_req_t *req)
{
    set_deep_sleep_state(false);

    httpd_resp_set_type(req, "application/json");
    cJSON *root = cJSON_CreateObject();

    cJSON_AddStringToObject(root, "BoardType", BoardType);
    cJSON_AddStringToObject(root, "Starttime", starttime_sta.c_str());

    std::string formatedUptime = getFormatedUptime(false);
    cJSON_AddStringToObject(root, "Uptime", formatedUptime.c_str());

    cJSON_AddStringToObject(root, "GitBranch", libfive_git_branch());
    cJSON_AddStringToObject(root, "GitTag", libfive_git_version());
    cJSON_AddStringToObject(root, "GitRevision", libfive_git_revision());

    cJSON_AddStringToObject(root, "BuildTime", build_time());
    cJSON_AddStringToObject(root, "FirmwareVersion", getFwVersion().c_str());
    cJSON_AddStringToObject(root, "HTMLVersion", getHTMLversion().c_str());

    cJSON_AddStringToObject(root, "Hostname", network_config.hostname.c_str());

    std::string *IPAddress = getIPAddress();
    cJSON_AddStringToObject(root, "IP", IPAddress->c_str());

    std::string *SSID = getSSID();
    cJSON_AddStringToObject(root, "SSID", SSID->c_str());

    cJSON_AddStringToObject(root, "SDCardPartitionSize", getSDCardPartitionSize().c_str());
    cJSON_AddStringToObject(root, "SDCardFreePartitionSpace", getSDCardFreePartitionSpace().c_str());
    cJSON_AddStringToObject(root, "SDCardPartitionAllocationSize", getSDCardPartitionAllocationSize().c_str());
    cJSON_AddStringToObject(root, "SDCardManufacturer", getSDCardManufacturer().c_str());
    cJSON_AddStringToObject(root, "SDCardName", getSDCardName().c_str());
    cJSON_AddStringToObject(root, "SDCardCapacity", getSDCardCapacity().c_str());
    cJSON_AddStringToObject(root, "SDCardSectorSize", getSDCardSectorSize().c_str());

    cJSON_AddStringToObject(root, "IDFVersion", IDF_VER);
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    // cJSON_AddNumberToObject(root, "ChipCores", chip_info.cores);
    cJSON_AddStringToObject(root, "ChipCores", std::to_string(chip_info.cores).c_str());
    // cJSON_AddNumberToObject(root, "ChipRevision", chip_info.revision);
    cJSON_AddStringToObject(root, "ChipRevision", std::to_string(chip_info.revision).c_str());
    // cJSON_AddNumberToObject(root, "ChipFeatures", chip_info.features);
    cJSON_AddStringToObject(root, "ChipFeatures", std::to_string(chip_info.features).c_str());

    cJSON_AddStringToObject(root, "FreeHeapSize", std::to_string(heap_caps_get_free_size(MALLOC_CAP_8BIT)).c_str());
    cJSON_AddStringToObject(root, "FreeSPIHeapSize", std::to_string(heap_caps_get_free_size(MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM)).c_str());
    cJSON_AddStringToObject(root, "FreeInternalHeapSize", std::to_string(heap_caps_get_free_size(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL)).c_str());
    cJSON_AddStringToObject(root, "HeapLargestFreeBlockSize", std::to_string(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM)).c_str());
    cJSON_AddStringToObject(root, "HeapIntLargestFreeBlockSize", std::to_string(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL)).c_str());
    cJSON_AddStringToObject(root, "MinFreeHeapSize", std::to_string(heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM)).c_str());
    cJSON_AddStringToObject(root, "MinFreeInternalHeapSize", std::to_string(heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL)).c_str());

    const char *sys_info = cJSON_Print(root);
    esp_err_t ret = httpd_resp_sendstr(req, sys_info);
    free((void *)sys_info);
    cJSON_Delete(root);

    return ret;
}

/* Handler for getting system status */
static esp_err_t system_status_get_handler(httpd_req_t *req)
{
    set_deep_sleep_state(false);

    httpd_resp_set_type(req, "application/json");
    cJSON *root = cJSON_CreateObject();

    // cJSON_AddStringToObject(root, "FlowStatus", flowctrl.getActStatus()->c_str());
    cJSON_AddStringToObject(root, "FlowStatus", flowctrl.getActStatusWithTime()->c_str());

    char Rounds[10] = "";
    snprintf(Rounds, sizeof(Rounds), "%d", getCountFlowRounds());
    cJSON_AddStringToObject(root, "Round", Rounds);

    cJSON_AddStringToObject(root, "Value", flowctrl.getReadoutAll(READOUT_TYPE_VALUE).c_str());
    cJSON_AddStringToObject(root, "PreValue", flowctrl.getReadoutAll(READOUT_TYPE_PREVALUE).c_str());
    cJSON_AddStringToObject(root, "RawValue", flowctrl.getReadoutAll(READOUT_TYPE_RAWVALUE).c_str());
    cJSON_AddStringToObject(root, "ValueStatus", flowctrl.getReadoutAll(READOUT_TYPE_ERROR).c_str());

    cJSON_AddStringToObject(root, "CpuTemp", std::to_string((int)temperatureRead()).c_str());

    const char *sys_info = cJSON_Print(root);
    esp_err_t ret = httpd_resp_sendstr(req, sys_info);
    free((void *)sys_info);
    cJSON_Delete(root);

    return ret;
}

#ifdef CONFIG_HTTPD_WS_SUPPORT
/*
 * async send function, which we put into the httpd work queue
 */
static void ws_async_send(void *req)
{
    /*
    #ifdef WS_SERVER_USE_PSRAM
        struct async_resp_req *resp_arg = (async_resp_req *)heap_caps_malloc((sizeof(struct async_resp_req)), MALLOC_CAP_SPIRAM);
    #else
        struct async_resp_req *resp_arg = (async_resp_req *)malloc(sizeof(struct async_resp_req));
    #endif
        resp_arg = (async_resp_req *)req;
    */
    struct async_resp_req *resp_arg = (async_resp_req *)req;

    httpd_handle_t hd = resp_arg->hd;
    int fd = resp_arg->fd;
    int ws_request = resp_arg->request;

    cJSON *root = cJSON_CreateObject();

    switch (ws_request)
    {
    case 0:
    {
        // BoardType
        cJSON_AddStringToObject(root, "BoardType", BoardType);
    }
    break;
    case 1:
    {
        // Starttime
        cJSON_AddStringToObject(root, "Starttime", starttime_sta.c_str());
    }
    break;
    case 2:
    {
        // GitBranch
        cJSON_AddStringToObject(root, "GitBranch", libfive_git_branch());
    }
    break;
    case 3:
    {
        // GitTag
        cJSON_AddStringToObject(root, "GitTag", libfive_git_version());
    }
    break;
    case 4:
    {
        // GitRevision
        cJSON_AddStringToObject(root, "GitRevision", libfive_git_revision());
    }
    break;
    case 5:
    {
        // BuildTime
        cJSON_AddStringToObject(root, "BuildTime", build_time());
    }
    break;
    case 6:
    {
        // FirmwareVersion
        cJSON_AddStringToObject(root, "FirmwareVersion", getFwVersion().c_str());
    }
    break;
    case 7:
    {
        // HTMLVersion
        cJSON_AddStringToObject(root, "HTMLVersion", getHTMLversion().c_str());
    }
    break;
    case 8:
    {
        // Hostname
        cJSON_AddStringToObject(root, "Hostname", network_config.hostname.c_str());
    }
    break;
    case 9:
    {
        // IP
        std::string *IPAddress = getIPAddress();
        cJSON_AddStringToObject(root, "IP", IPAddress->c_str());
    }
    break;
    case 10:
    {
        // SSID
        std::string *SSID = getSSID();
        cJSON_AddStringToObject(root, "SSID", SSID->c_str());
    }
    break;
    case 11:
    {
        // FlowStatus
        // cJSON_AddStringToObject(root, "FlowStatus", flowctrl.getActStatus()->c_str());
        cJSON_AddStringToObject(root, "FlowStatus", flowctrl.getActStatusWithTime()->c_str());
    }
    break;
    case 12:
    {
        // Round
        char Rounds[10] = "";
        snprintf(Rounds, sizeof(Rounds), "%d", getCountFlowRounds());
        cJSON_AddStringToObject(root, "Round", Rounds);
    }
    break;
    case 13:
    {
        // SDCardPartitionSize
        cJSON_AddStringToObject(root, "SDCardPartitionSize", getSDCardPartitionSize().c_str());
    }
    break;
    case 14:
    {
        // SDCardFreePartitionSpace
        cJSON_AddStringToObject(root, "SDCardFreePartitionSpace", getSDCardFreePartitionSpace().c_str());
    }
    break;
    case 15:
    {
        // SDCardPartitionAllocationSize
        cJSON_AddStringToObject(root, "SDCardPartitionAllocationSize", getSDCardPartitionAllocationSize().c_str());
    }
    break;
    case 16:
    {
        // SDCardManufacturer
        cJSON_AddStringToObject(root, "SDCardManufacturer", getSDCardManufacturer().c_str());
    }
    break;
    case 17:
    {
        // SDCardName
        cJSON_AddStringToObject(root, "SDCardName", getSDCardName().c_str());
    }
    break;
    case 18:
    {
        // SDCardCapacity
        cJSON_AddStringToObject(root, "SDCardCapacity", getSDCardCapacity().c_str());
    }
    break;
    case 19:
    {
        // SDCardSectorSize
        cJSON_AddStringToObject(root, "SDCardSectorSize", getSDCardSectorSize().c_str());
    }
    break;
    case 20:
    {
        // IDFVersion
        cJSON_AddStringToObject(root, "IDFVersion", IDF_VER);
    }
    break;
    case 21:
    {
        // ChipCores
        esp_chip_info_t chip_info;
        esp_chip_info(&chip_info);
        // cJSON_AddNumberToObject(root, "ChipCores", chip_info.cores);
        cJSON_AddStringToObject(root, "ChipCores", std::to_string(chip_info.cores).c_str());
    }
    break;
    case 22:
    {
        // ChipRevision
        esp_chip_info_t chip_info;
        esp_chip_info(&chip_info);
        // cJSON_AddNumberToObject(root, "ChipRevision", chip_info.revision);
        cJSON_AddStringToObject(root, "ChipRevision", std::to_string(chip_info.revision).c_str());
    }
    break;
    case 23:
    {
        // ChipFeatures
        esp_chip_info_t chip_info;
        esp_chip_info(&chip_info);
        // cJSON_AddNumberToObject(root, "ChipFeatures", chip_info.features);
        cJSON_AddStringToObject(root, "ChipFeatures", std::to_string(chip_info.features).c_str());
    }
    break;
    case 24:
    {
        // cputemp
        // cJSON_AddNumberToObject(root, "CpuTemp", (int)temperatureRead());
        cJSON_AddStringToObject(root, "CpuTemp", std::to_string((int)temperatureRead()).c_str());
    }
    break;
    case 25:
    {
        // current date
        std::string formatedDateAndTime = getCurrentTimeString("%Y-%m-%d %H:%M:%S");
        cJSON_AddStringToObject(root, "Date", formatedDateAndTime.c_str());
    }
    break;
    case 26:
    {
        // uptime
        std::string formatedUptime = getFormatedUptime(false);
        cJSON_AddStringToObject(root, "Uptime", formatedUptime.c_str());
    }
    break;
    case 27:
    {
        if (network_config.connection_type == NETWORK_CONNECTION_WIFI_STA)
        {
            if (getWIFIisConnected())
            {
                // verbunden, rssi vorhanden
                // cJSON_AddNumberToObject(root, "RSSI", get_WIFI_RSSI());
                cJSON_AddStringToObject(root, "RSSI", std::to_string(get_WIFI_RSSI()).c_str());
            }
            else
            {
                // nicht verbunden, rssi nicht vorhanden
                // cJSON_AddNumberToObject(root, "RSSI", 127);
                cJSON_AddStringToObject(root, "RSSI", "127");
            }
        }
#if (CONFIG_ETH_ENABLED && CONFIG_ETH_USE_SPI_ETHERNET && CONFIG_ETH_SPI_ETHERNET_W5500)
        else if (network_config.connection_type == NETWORK_CONNECTION_ETH)
        {
            if (getETHisConnected())
            {
                // verbunden, rssi 0
                // cJSON_AddNumberToObject(root, "RSSI", 0);
                cJSON_AddStringToObject(root, "RSSI", "0");
            }
            else
            {
                // nicht verbunden, rssi nicht vorhanden
                // cJSON_AddNumberToObject(root, "RSSI", 127);
                cJSON_AddStringToObject(root, "RSSI", "127");
            }
        }
#endif
    }
    break;
    case 28:
    {
        // Value
        cJSON_AddStringToObject(root, "Value", flowctrl.getReadoutAll(READOUT_TYPE_VALUE).c_str());
    }
    break;
    case 29:
    {
        // PreValue
        cJSON_AddStringToObject(root, "PreValue", flowctrl.getReadoutAll(READOUT_TYPE_PREVALUE).c_str());
    }
    break;
    case 30:
    {
        // RawValue
        cJSON_AddStringToObject(root, "RawValue", flowctrl.getReadoutAll(READOUT_TYPE_RAWVALUE).c_str());
    }
    break;
    case 31:
    {
        // Readout ERROR
        cJSON_AddStringToObject(root, "ValueStatus", flowctrl.getReadoutAll(READOUT_TYPE_ERROR).c_str());
    }
    break;
    case 100:
    {
        cJSON_AddStringToObject(root, "BoardType", BoardType);
        cJSON_AddStringToObject(root, "Starttime", starttime_sta.c_str());

        std::string formatedUptime = getFormatedUptime(false);
        cJSON_AddStringToObject(root, "Uptime", formatedUptime.c_str());

        cJSON_AddStringToObject(root, "GitBranch", libfive_git_branch());
        cJSON_AddStringToObject(root, "GitTag", libfive_git_version());
        cJSON_AddStringToObject(root, "GitRevision", libfive_git_revision());

        cJSON_AddStringToObject(root, "BuildTime", build_time());
        cJSON_AddStringToObject(root, "FirmwareVersion", getFwVersion().c_str());
        cJSON_AddStringToObject(root, "HTMLVersion", getHTMLversion().c_str());

        cJSON_AddStringToObject(root, "Hostname", network_config.hostname.c_str());

        std::string *IPAddress = getIPAddress();
        cJSON_AddStringToObject(root, "IP", IPAddress->c_str());

        std::string *SSID = getSSID();
        cJSON_AddStringToObject(root, "SSID", SSID->c_str());

        cJSON_AddStringToObject(root, "SDCardPartitionSize", getSDCardPartitionSize().c_str());
        cJSON_AddStringToObject(root, "SDCardFreePartitionSpace", getSDCardFreePartitionSpace().c_str());
        cJSON_AddStringToObject(root, "SDCardPartitionAllocationSize", getSDCardPartitionAllocationSize().c_str());
        cJSON_AddStringToObject(root, "SDCardManufacturer", getSDCardManufacturer().c_str());
        cJSON_AddStringToObject(root, "SDCardName", getSDCardName().c_str());
        cJSON_AddStringToObject(root, "SDCardCapacity", getSDCardCapacity().c_str());
        cJSON_AddStringToObject(root, "SDCardSectorSize", getSDCardSectorSize().c_str());

        cJSON_AddStringToObject(root, "IDFVersion", IDF_VER);
        esp_chip_info_t chip_info;
        esp_chip_info(&chip_info);
        // cJSON_AddNumberToObject(root, "ChipCores", chip_info.cores);
        cJSON_AddStringToObject(root, "ChipCores", std::to_string(chip_info.cores).c_str());
        // cJSON_AddNumberToObject(root, "ChipRevision", chip_info.revision);
        cJSON_AddStringToObject(root, "ChipRevision", std::to_string(chip_info.revision).c_str());
        // cJSON_AddNumberToObject(root, "ChipFeatures", chip_info.features);
        cJSON_AddStringToObject(root, "ChipFeatures", std::to_string(chip_info.features).c_str());

        cJSON_AddStringToObject(root, "FreeHeapSize", std::to_string(heap_caps_get_free_size(MALLOC_CAP_8BIT)).c_str());
        cJSON_AddStringToObject(root, "FreeSPIHeapSize", std::to_string(heap_caps_get_free_size(MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM)).c_str());
        cJSON_AddStringToObject(root, "FreeInternalHeapSize", std::to_string(heap_caps_get_free_size(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL)).c_str());
        cJSON_AddStringToObject(root, "HeapLargestFreeBlockSize", std::to_string(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM)).c_str());
        cJSON_AddStringToObject(root, "HeapIntLargestFreeBlockSize", std::to_string(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL)).c_str());
        cJSON_AddStringToObject(root, "MinFreeHeapSize", std::to_string(heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM)).c_str());
        cJSON_AddStringToObject(root, "MinFreeInternalHeapSize", std::to_string(heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL)).c_str());
    }
    break;
    case 101:
    {
        // cJSON_AddStringToObject(root, "FlowStatus", flowctrl.getActStatus()->c_str());
        cJSON_AddStringToObject(root, "FlowStatus", flowctrl.getActStatusWithTime()->c_str());

        char Rounds[10] = "";
        snprintf(Rounds, sizeof(Rounds), "%d", getCountFlowRounds());
        cJSON_AddStringToObject(root, "Round", Rounds);

        cJSON_AddStringToObject(root, "Value", flowctrl.getReadoutAll(READOUT_TYPE_VALUE).c_str());
        cJSON_AddStringToObject(root, "PreValue", flowctrl.getReadoutAll(READOUT_TYPE_PREVALUE).c_str());
        cJSON_AddStringToObject(root, "RawValue", flowctrl.getReadoutAll(READOUT_TYPE_RAWVALUE).c_str());
        cJSON_AddStringToObject(root, "ValueStatus", flowctrl.getReadoutAll(READOUT_TYPE_ERROR).c_str());

        cJSON_AddStringToObject(root, "CpuTemp", std::to_string((int)temperatureRead()).c_str());
    }
    break;
    default:
    {
    }
    break;
    }

    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));

    const char *sys_info = cJSON_Print(root);
    ws_pkt.payload = (uint8_t *)sys_info;
    ws_pkt.len = strlen(sys_info);
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;

    httpd_ws_send_frame_async(hd, fd, &ws_pkt);

#ifdef WS_SERVER_USE_PSRAM
    heap_caps_free(resp_arg);
#else
    free(resp_arg);
#endif

    free((void *)sys_info);
    cJSON_Delete(root);
}

static esp_err_t trigger_async_send(httpd_handle_t handle, httpd_req_t *req, int ws_request)
{
#ifdef WS_SERVER_USE_PSRAM
    struct async_resp_req *resp_arg = (async_resp_req *)heap_caps_malloc((sizeof(struct async_resp_req)), MALLOC_CAP_SPIRAM);
#else
    struct async_resp_req *resp_arg = (async_resp_req *)malloc(sizeof(struct async_resp_req));
#endif

    if (resp_arg == NULL)
    {
        return ESP_ERR_NO_MEM;
    }

    resp_arg->hd = req->handle;
    resp_arg->fd = httpd_req_to_sockfd(req);
    resp_arg->request = ws_request;
    esp_err_t ret = httpd_queue_work(handle, ws_async_send, resp_arg);
    if (ret != ESP_OK)
    {
#ifdef WS_SERVER_USE_PSRAM
        heap_caps_free(resp_arg);
#else
        free(resp_arg);
#endif
    }

    return ret;
}

/*
 * This handler echos back the received ws data
 * and triggers an async send if certain message received
 */
static esp_err_t ws_echo_handler(httpd_req_t *req)
{
    set_deep_sleep_state(false);

    if (req->method == HTTP_GET)
    {
        ESP_LOGD(TAG, "Handshake done, the new connection was opened");
        return ESP_OK;
    }

    httpd_ws_frame_t ws_pkt;
    uint8_t *buf = NULL;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;

    /* Set max_len = 0 to get the frame len */
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "httpd_ws_recv_frame failed to get frame len with %d", ret);
        return ret;
    }

    ESP_LOGD(TAG, "frame len is %d", ws_pkt.len);
    if (ws_pkt.len)
    {
        /* ws_pkt.len + 1 is for NULL termination as we are expecting a string */
#ifdef WS_SERVER_USE_PSRAM
        buf = (uint8_t *)heap_caps_calloc(1, (ws_pkt.len + 1), MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
#else
        buf = (uint8_t *)calloc(1, ws_pkt.len + 1);
#endif
        if (buf == NULL)
        {
            ESP_LOGE(TAG, "Failed to calloc memory for buf");
            return ESP_ERR_NO_MEM;
        }

        ws_pkt.payload = buf;

        /* Set max_len = ws_pkt.len to get the frame payload */
        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "httpd_ws_recv_frame failed with %d", ret);
#ifdef WS_SERVER_USE_PSRAM
            heap_caps_free(buf);
#else
            free(buf);
#endif
            return ret;
        }
        ESP_LOGD(TAG, "Got packet with message: %s", ws_pkt.payload);
    }

    ESP_LOGD(TAG, "Packet type: %d", ws_pkt.type);
    if (ws_pkt.type == HTTPD_WS_TYPE_TEXT)
    {
        int ws_request_index = -1;

        if (strcmp((char *)ws_pkt.payload, "BoardType") == 0)
        {
            ws_request_index = 0;
        }
        else if (strcmp((char *)ws_pkt.payload, "Starttime") == 0)
        {
            ws_request_index = 1;
        }
        else if (strcmp((char *)ws_pkt.payload, "GitBranch") == 0)
        {
            ws_request_index = 2;
        }
        else if (strcmp((char *)ws_pkt.payload, "GitTag") == 0)
        {
            ws_request_index = 3;
        }
        else if (strcmp((char *)ws_pkt.payload, "GitRevision") == 0)
        {
            ws_request_index = 4;
        }
        else if (strcmp((char *)ws_pkt.payload, "BuildTime") == 0)
        {
            ws_request_index = 5;
        }
        else if (strcmp((char *)ws_pkt.payload, "FirmwareVersion") == 0)
        {
            ws_request_index = 6;
        }
        else if (strcmp((char *)ws_pkt.payload, "HTMLVersion") == 0)
        {
            ws_request_index = 7;
        }
        else if (strcmp((char *)ws_pkt.payload, "Hostname") == 0)
        {
            ws_request_index = 8;
        }
        else if (strcmp((char *)ws_pkt.payload, "IP") == 0)
        {
            ws_request_index = 9;
        }
        else if (strcmp((char *)ws_pkt.payload, "SSID") == 0)
        {
            ws_request_index = 10;
        }
        else if (strcmp((char *)ws_pkt.payload, "FlowStatus") == 0)
        {
            ws_request_index = 11;
        }
        else if (strcmp((char *)ws_pkt.payload, "Round") == 0)
        {
            ws_request_index = 12;
        }
        else if (strcmp((char *)ws_pkt.payload, "SDCardPartitionSize") == 0)
        {
            ws_request_index = 13;
        }
        else if (strcmp((char *)ws_pkt.payload, "SDCardFreePartitionSpace") == 0)
        {
            ws_request_index = 14;
        }
        else if (strcmp((char *)ws_pkt.payload, "SDCardPartitionAllocationSize") == 0)
        {
            ws_request_index = 15;
        }
        else if (strcmp((char *)ws_pkt.payload, "SDCardManufacturer") == 0)
        {
            ws_request_index = 16;
        }
        else if (strcmp((char *)ws_pkt.payload, "SDCardName") == 0)
        {
            ws_request_index = 17;
        }
        else if (strcmp((char *)ws_pkt.payload, "SDCardCapacity") == 0)
        {
            ws_request_index = 18;
        }
        else if (strcmp((char *)ws_pkt.payload, "SDCardSectorSize") == 0)
        {
            ws_request_index = 19;
        }
        else if (strcmp((char *)ws_pkt.payload, "IDFVersion") == 0)
        {
            ws_request_index = 20;
        }
        else if (strcmp((char *)ws_pkt.payload, "ChipCores") == 0)
        {
            ws_request_index = 21;
        }
        else if (strcmp((char *)ws_pkt.payload, "ChipRevision") == 0)
        {
            ws_request_index = 22;
        }
        else if (strcmp((char *)ws_pkt.payload, "ChipFeatures") == 0)
        {
            ws_request_index = 23;
        }
        else if (strcmp((char *)ws_pkt.payload, "CpuTemp") == 0)
        {
            ws_request_index = 24;
        }
        else if (strcmp((char *)ws_pkt.payload, "Date") == 0)
        {
            ws_request_index = 25;
        }
        else if (strcmp((char *)ws_pkt.payload, "Uptime") == 0)
        {
            ws_request_index = 26;
        }
        else if (strcmp((char *)ws_pkt.payload, "RSSI") == 0)
        {
            ws_request_index = 27;
        }
        else if (strcmp((char *)ws_pkt.payload, "Value") == 0)
        {
            ws_request_index = 28;
        }
        else if (strcmp((char *)ws_pkt.payload, "PreValue") == 0)
        {
            ws_request_index = 29;
        }
        else if (strcmp((char *)ws_pkt.payload, "RawValue") == 0)
        {
            ws_request_index = 30;
        }
        else if (strcmp((char *)ws_pkt.payload, "ValueStatus") == 0)
        {
            ws_request_index = 31;
        }
        else if (strcmp((char *)ws_pkt.payload, "SystemInfo") == 0)
        {
            ws_request_index = 100;
        }
        else if (strcmp((char *)ws_pkt.payload, "SystemStatus") == 0)
        {
            ws_request_index = 101;
        }

#ifdef WS_SERVER_USE_PSRAM
        heap_caps_free(buf);
#else
        free(buf);
#endif
        return trigger_async_send(req->handle, req, ws_request_index);
    }

    ret = httpd_ws_send_frame(req, &ws_pkt);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "httpd_ws_send_frame failed with %d", ret);
    }
#ifdef WS_SERVER_USE_PSRAM
    heap_caps_free(buf);
#else
    free(buf);
#endif

    return ret;
}
#endif

esp_err_t register_server_main_uri(httpd_handle_t my_server)
{
    esp_err_t ret = ESP_OK;

    /* URI handler for fetching system info */
    httpd_uri_t system_info_get_handle = {
        .uri = "/system_info",
        .method = HTTP_GET,
        .handler = APPLY_BASIC_AUTH_FILTER(system_info_get_handler),
        .user_ctx = rest_context // Pass server data as context
    };
    ret |= httpd_register_uri_handler(my_server, &system_info_get_handle);

    /* URI handler for fetching system info */
    httpd_uri_t system_status_get_handle = {
        .uri = "/system_status",
        .method = HTTP_GET,
        .handler = APPLY_BASIC_AUTH_FILTER(system_status_get_handler),
        .user_ctx = rest_context // Pass server data as context
    };
    ret |= httpd_register_uri_handler(my_server, &system_status_get_handle);

    httpd_uri_t ws_get_handle = {
        .uri = "/ws",
        .method = HTTP_GET,
        .handler = APPLY_BASIC_AUTH_FILTER(ws_echo_handler),
        .user_ctx = rest_context, // Pass server data as context
        .is_websocket = true,
    };
    ret |= httpd_register_uri_handler(my_server, &ws_get_handle);

    /* URI handler for getting web server files */
    httpd_uri_t common_get_uri = {
        .uri = "/*",
        .method = HTTP_GET,
        .handler = APPLY_BASIC_AUTH_FILTER(main_server_handler),
        .user_ctx = rest_context // Pass server data as context
    };
    ret |= httpd_register_uri_handler(my_server, &common_get_uri);

    return ret;
}
