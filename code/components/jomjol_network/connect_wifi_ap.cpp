/*  WiFi softAP Example
   This example code is in the Public Domain (or CC0 licensed, at your option.)
   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include "connect_wifi_ap.h"

#include <string.h>
#include <string>
#include <stdio.h>
#include <esp_mac.h>
#include <esp_wifi.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <lwip/err.h>
#include <lwip/sys.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/event_groups.h>

#include "defines.h"
#include "Helper.h"

#include "ClassLogFile.h"
#include "server_help.h"
#include "statusled.h"
#include "server_ota.h"
#include "basic_auth.h"

static const char *TAG = "WIFI AP";

/* The examples use WiFi configuration that you can set via project configuration menu.
   If you'd rather not, just change the below entries to strings with
   the config you want - ie #define EXAMPLE_WIFI_SSID "mywifissid"
*/

esp_netif_t *my_netif_ap = NULL;

static bool WIFIInitialized = false;
static bool WIFIConnected = false;
static bool WIFIConnectionSuccessful = false;
static int WIFIReconnectCnt = 0;

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_id == WIFI_EVENT_AP_STACONNECTED)
    {
        wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
        ESP_LOGI(TAG, "station " MACSTR " join, AID=%d", MAC2STR(event->mac), event->aid);
    }
    else if (event_id == WIFI_EVENT_AP_STADISCONNECTED)
    {
        wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)event_data;
        ESP_LOGI(TAG, "station " MACSTR " leave, AID=%d", MAC2STR(event->mac), event->aid);
    }
}

esp_err_t wifi_init_ap(void)
{
    LogFile.WriteToFile(ESP_LOG_INFO, TAG, "WiFi AP init...");

    // Set log level for netif/wifi component to WARN level (default: INFO; only relevant for serial console)
    // ********************************************
    esp_log_level_set("netif", ESP_LOG_WARN);
    esp_log_level_set("wifi", ESP_LOG_WARN);

    esp_err_t ret = esp_netif_init();
    if (ret != ESP_OK)
    {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "esp_netif_init: Error: " + std::string(esp_err_to_name(ret)));
        return ret;
    }

    ret = esp_event_loop_create_default();
    if (ret != ESP_OK)
    {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "esp_event_loop_create_default: Error: " + std::string(esp_err_to_name(ret)));
        return ret;
    }

    my_netif_ap = esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK)
    {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "esp_wifi_init: Error: " + std::string(esp_err_to_name(ret)));
        return ret;
    }

    ret = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL);
    if (ret != ESP_OK)
    {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "esp_event_handler_instance_register - WIFI_ANY: Error: " + std::string(esp_err_to_name(ret)));
        return ret;
    }

    wifi_config_t wifi_config = {};

    strcpy((char *)wifi_config.ap.ssid, (const char *)ESP_WIFI_AP_SSID);
    strcpy((char *)wifi_config.ap.password, (const char *)ESP_WIFI_AP_PASS);
    wifi_config.ap.channel = ESP_WIFI_AP_CHANNEL;
    wifi_config.ap.max_connection = ESP_WIFI_AP_MAX_STA_CONN;
    wifi_config.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;

    if (strlen(ESP_WIFI_AP_PASS) == 0)
    {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ret = esp_wifi_set_mode(WIFI_MODE_AP);
    if (ret != ESP_OK)
    {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "esp_wifi_set_mode: Error: " + std::string(esp_err_to_name(ret)));
        return ret;
    }

    ret = esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
    if (ret != ESP_OK)
    {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "esp_wifi_set_config: Error: " + std::string(esp_err_to_name(ret)));
        return ret;
    }

    ret = esp_wifi_start();
    if (ret != ESP_OK)
    {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "esp_wifi_start: Error: " + std::string(esp_err_to_name(ret)));
        return ret;
    }

    ESP_LOGI(TAG, "started with SSID \"%s\", password: \"%s\", channel: %d. Connect to AP and open http://%s", ESP_WIFI_AP_SSID, ESP_WIFI_AP_PASS, ESP_WIFI_AP_CHANNEL, ESP_WIFI_AP_IP);

    LogFile.WriteToFile(ESP_LOG_INFO, TAG, "WiFi AP init successful done");
    return ESP_OK;
}

void wifi_deinit_ap(void)
{
    LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Wifi AP deinit...");

    WIFIInitialized = false;

    ESP_LOGD(TAG, "esp_wifi_disconnect()");
    ESP_ERROR_CHECK(esp_wifi_disconnect());

    ESP_LOGD(TAG, "esp_wifi_stop()");
    ESP_ERROR_CHECK(esp_wifi_stop());

    ESP_LOGD(TAG, "esp_netif_destroy(my_ap_netif)");
    esp_netif_destroy(my_netif_ap);

    ESP_LOGD(TAG, "esp_netif_deinit()");
    ESP_ERROR_CHECK(esp_netif_deinit());

    ESP_LOGD(TAG, "esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler)");
    ESP_ERROR_CHECK(esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler));

    ESP_LOGD(TAG, "esp_wifi_deinit()");
    ESP_ERROR_CHECK(esp_wifi_deinit());

    LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Wifi AP deinit done");
}

bool getWIFIAPisConnected(void)
{
    return WIFIConnected;
}
