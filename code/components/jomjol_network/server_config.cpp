#include "server_config.h"

#include <string>
#include <stdio.h>
#include <esp_log.h>
#include <esp_wifi.h>
#include <netdb.h>
#include <esp_chip_info.h>

#include "defines.h"
#include "Helper.h"

#include "MainFlowControl.h"
#include "ClassLogFile.h"

#include "read_network_config.h"
#include "connect_wifi_ap.h"
#include "connect_wifi_sta.h"

#include "server_help.h"
#include "server_file.h"
#include "server_ota.h"
#include "server_camera.h"

#ifdef ENABLE_MQTT
#include "server_mqtt.h"
#endif // ENABLE_MQTT

#include "basic_auth.h"
#include "time_sntp.h"

#include "statusled.h"

static const char *TAG = "AP SERVER";

httpd_handle_t my_server_ap = NULL;
httpd_config_t my_config_ap = HTTPD_DEFAULT_CONFIG();
std::string starttime_ap = "";

bool isConfigINI = false;
bool isNetworkINI = false;

esp_err_t Init_Server_Config(void)
{
    // Start ap server + register handler
    // ********************************************
    ESP_LOGD(TAG, "starting ap servers");

    esp_err_t ret = ESP_OK;

    ret = start_webserverAP();
    if (ret != ESP_OK)
    {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "start_webserverAP: Error: " + std::string(esp_err_to_name(ret)));
        return ret; // No way to continue without working WLAN!
    }

    ESP_LOGD(TAG, "Before reg ap server uri");
    register_server_ap_uri(my_server_ap);

    return ret;
}

esp_err_t start_webserverAP(void)
{
    esp_err_t ret = ESP_OK;

    my_config_ap.task_priority = tskIDLE_PRIORITY + 3;
    my_config_ap.stack_size = 6 * 2048;
    my_config_ap.core_id = 1;
    my_config_ap.server_port = 80;
    my_config_ap.ctrl_port = 32768;
    my_config_ap.max_open_sockets = 5;
    my_config_ap.max_uri_handlers = 8; // Make sure this fits all URI handlers. Memory usage in bytes: 6*max_uri_handlers
    my_config_ap.max_resp_headers = 8;
    my_config_ap.backlog_conn = 5;
    my_config_ap.lru_purge_enable = true; // this cuts old connections if new ones are needed.
    my_config_ap.recv_wait_timeout = 5;
    my_config_ap.send_wait_timeout = 5;
    my_config_ap.global_user_ctx = NULL;
    my_config_ap.global_user_ctx_free_fn = NULL;
    my_config_ap.global_transport_ctx = NULL;
    my_config_ap.global_transport_ctx_free_fn = NULL;
    my_config_ap.open_fn = NULL;
    my_config_ap.close_fn = NULL;
    my_config_ap.uri_match_fn = NULL;
    my_config_ap.uri_match_fn = httpd_uri_match_wildcard;

    starttime_ap = getCurrentTimeString("%Y%m%d-%H%M%S");

    // Start the httpd server
    ESP_LOGI(TAG, "Starting server on port: '%d'", my_config_ap.server_port);
    ret = httpd_start(&my_server_ap, &my_config_ap);
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

    ESP_LOGI(TAG, "Registering URI handlers : Instance created successfully");
    return ret;
}

void stop_webserverAP(httpd_handle_t server)
{
    httpd_stop(server);
}

void SendHTTPResponse(httpd_req_t *req)
{
    std::string message = "<h1>AI-on-the-edge - BASIC SETUP</h1><p>This is an access point with a minimal server to setup the minimum required files and information on the device and the SD-card. ";
    message += "This mode is always started if one of the following files is missing: /wlan.ini or the /config/config.ini.<p>";
    message += "The setup is done in 3 steps: 1. upload full inital configuration (sd-card content), 2. store WLAN access information, 3. reboot (and connect to WLANs)<p><p>";
    message += "Please follow the below instructions.<p>";
    httpd_resp_send_chunk(req, message.c_str(), strlen(message.c_str()));

    if (!isConfigINI)
    {
        message = "<h3>1. Upload initial configuration to sd-card</h3><p>";
        message += "The configuration file config.ini is missing and most propably the full configuration and html folder on the sd-card. ";
        message += "This is normal after the first flashing of the firmware and an empty sd-card. Please upload \"remote_setup.zip\", which contains a full inital configuration.<p>";
        message += "<input id=\"newfile\" type=\"file\"><br>";
        message += "<button class=\"button\" style=\"width:300px\" id=\"doUpdate\" type=\"button\" onclick=\"upload()\">Upload File</button><p>";
        message += "The upload might take up to 60s. After a succesfull upload the page will be updated.";
        httpd_resp_send_chunk(req, message.c_str(), strlen(message.c_str()));

        message = "<script language=\"JavaScript\">";
        message += "function upload() {";
        message += "var xhttp = new XMLHttpRequest();";
        message += "xhttp.onreadystatechange = function() {if (xhttp.readyState == 4) {if (xhttp.status == 200) {location.reload();}}};";
        message += "var filePath = document.getElementById(\"newfile\").value.split(/[\\\\/]/).pop();";
        message += "var file = document.getElementById(\"newfile\").files[0];";
        message += "if (!file.name.includes(\"remote-setup\")){if (!confirm(\"The zip file name should contain '...remote-setup...'. Are you sure that you have downloaded the correct file?\"))return;};";
        message += "var upload_path = \"/upload/firmware/\" + filePath; xhttp.open(\"POST\", upload_path, true); xhttp.send(file);document.reload();";
        message += "document.getElementById(\"doUpdate\").disabled = true;}";
        message += "</script>";
        httpd_resp_send_chunk(req, message.c_str(), strlen(message.c_str()));
        return;
    }

    isNetworkINI = FileExists(NETWORK_CONFIG_FILE);
    if (!isNetworkINI)
    {
        message = "<h3>2. WLAN access credentials</h3><p>";
        message += "<table>";
        message += "<tr><td>WLAN-SSID</td><td><input type=\"text\" name=\"ssid\" id=\"ssid\"></td><td>SSID of the WLAN</td></tr>";
        message += "<tr><td>WLAN-Password</td><td><input type=\"text\" name=\"password\" id=\"password\"></td><td>ATTENTION: the password will not be encrypted during the sending.</td><tr>";
        message += "</table><p>";
        message += "<h4>ATTENTION:<h4>Be sure about the WLAN settings. They cannot be reset afterwards. If ssid or password is wrong, you need to take out the sd-card and manually change them in \"wlan.ini\"!<p>";
        httpd_resp_send_chunk(req, message.c_str(), strlen(message.c_str()));

        //        message = "</tr><tr><td> Hostname</td><td><input type=\"text\" name=\"hostname\" id=\"hostname\"></td><td></td>";
        //        message += "</tr><tr><td>Fixed IP</td><td><input type=\"text\" name=\"ip\" id=\"ip\"></td><td>Leave emtpy if set by router (DHCP)</td></tr>";
        //        message += "<tr><td>Gateway</td><td><input type=\"text\" name=\"gateway\" id=\"gateway\"></td><td>Leave emtpy if set by router (DHCP)</td></tr>";
        //        message += "<tr><td>Netmask</td><td><input type=\"text\" name=\"netmask\" id=\"netmask\"></td><td>Leave emtpy if set by router (DHCP)</td>";
        //        message += "</tr><tr><td>DNS</td><td><input type=\"text\" name=\"dns\" id=\"dns\"></td><td>Leave emtpy if set by router (DHCP)</td></tr>";
        //        message += "<tr><td>RSSI Threshold</td><td><input type=\"number\" name=\"name\" id=\"threshold\" min=\"-100\"  max=\"0\" step=\"1\" value = \"0\"></td><td>WLAN Mesh Parameter: Threshold for RSSI value to check for start switching access point in a mesh system (if actual RSSI is lower). Possible values: -100 to 0, 0 = disabled - Value will be transfered to wlan.ini at next startup)</td></tr>";
        //        httpd_resp_send_chunk(req, message.c_str(), strlen(message.c_str()));

        message = "<button class=\"button\" type=\"button\" onclick=\"wr()\">Write wlan.ini</button>";
        message += "<script language=\"JavaScript\">async function wr(){";
        message += "api = \"/config?\"+\"ssid=\"+document.getElementById(\"ssid\").value+\"&pwd=\"+document.getElementById(\"password\").value;";
        //        message += "api = \"/config?\"+\"ssid=\"+document.getElementById(\"ssid\").value+\"&pwd=\"+document.getElementById(\"password\").value+\"&hn=\"+document.getElementById(\"hostname\").value+\"&ip=\"+document.getElementById(\"ip\").value+\"&gw=\"+document.getElementById(\"gateway\").value+\"&nm=\"+document.getElementById(\"netmask\").value+\"&dns=\"+document.getElementById(\"dns\").value+\"&rssithreshold=\"+document.getElementById(\"threshold\").value;";
        message += "fetch(api);await new Promise(resolve => setTimeout(resolve, 1000));location.reload();}</script>";
        httpd_resp_send_chunk(req, message.c_str(), strlen(message.c_str()));
        return;
    }

    message = "<h3>3. Reboot</h3><p>";
    message += "After triggering the reboot, the zip-files gets extracted and written to the sd-card.<br>The ESP32 will restart two times and then connect to your access point. Please find the IP in your router settings and access it with the new ip-address.<p>";
    message += "The first update and initialization process can take up to 3 minutes before you find it in the wlan. Error logs can be found on the console / serial logout.<p>Have fun!<p>";
    message += "<button class=\"button\" type=\"button\" onclick=\"rb()\")>Reboot to first setup.</button>";
    message += "<script language=\"JavaScript\">async function rb(){";
    message += "api = \"/reboot\";";
    message += "fetch(api);await new Promise(resolve => setTimeout(resolve, 1000));location.reload();}</script>";
    httpd_resp_send_chunk(req, message.c_str(), strlen(message.c_str()));
}

static esp_err_t test_handler(httpd_req_t *req)
{
    SendHTTPResponse(req);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t reboot_handler(httpd_req_t *req)
{
    LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Trigger reboot due to firmware update.");
    doRebootOTA();
    return ESP_OK;
}

static esp_err_t config_ini_handler(httpd_req_t *req)
{
    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "config_ini_handler");
    char _query[400];
    char _valuechar[100];

    std::string fn = "/sdcard/firmware/";
    std::string _task = "";
    std::string ssid = "";
    std::string pwd = "";
    std::string hn = ""; // hostname
    std::string ip = "";
    std::string gw = ""; // gateway
    std::string nm = ""; // netmask
    std::string dns = "";
    std::string rssithreshold = ""; // rssi threshold for WIFI roaming
    std::string text = "";

    if (httpd_req_get_url_query_str(req, _query, sizeof(_query)) == ESP_OK)
    {
        ESP_LOGD(TAG, "Query: %s", _query);

        if (httpd_query_key_value(_query, "ssid", _valuechar, sizeof(_valuechar)) == ESP_OK)
        {
            ESP_LOGD(TAG, "ssid is found: %s", _valuechar);
            ssid = UrlDecode(std::string(_valuechar));
        }

        if (httpd_query_key_value(_query, "pwd", _valuechar, sizeof(_valuechar)) == ESP_OK)
        {
            ESP_LOGD(TAG, "pwd is found: %s", _valuechar);
            pwd = UrlDecode(std::string(_valuechar));
        }

        if (httpd_query_key_value(_query, "ssid", _valuechar, sizeof(_valuechar)) == ESP_OK)
        {
            ESP_LOGD(TAG, "ssid is found: %s", _valuechar);
            ssid = UrlDecode(std::string(_valuechar));
        }

        if (httpd_query_key_value(_query, "hn", _valuechar, sizeof(_valuechar)) == ESP_OK)
        {
            ESP_LOGD(TAG, "hostname is found: %s", _valuechar);
            hn = UrlDecode(std::string(_valuechar));
        }

        if (httpd_query_key_value(_query, "ip", _valuechar, sizeof(_valuechar)) == ESP_OK)
        {
            ESP_LOGD(TAG, "ip is found: %s", _valuechar);
            ip = UrlDecode(std::string(_valuechar));
        }

        if (httpd_query_key_value(_query, "gw", _valuechar, sizeof(_valuechar)) == ESP_OK)
        {
            ESP_LOGD(TAG, "gateway is found: %s", _valuechar);
            gw = UrlDecode(std::string(_valuechar));
        }

        if (httpd_query_key_value(_query, "nm", _valuechar, sizeof(_valuechar)) == ESP_OK)
        {
            ESP_LOGD(TAG, "netmask is found: %s", _valuechar);
            nm = UrlDecode(std::string(_valuechar));
        }

        if (httpd_query_key_value(_query, "dns", _valuechar, sizeof(_valuechar)) == ESP_OK)
        {
            ESP_LOGD(TAG, "dns is found: %s", _valuechar);
            dns = UrlDecode(std::string(_valuechar));
        }

        if (httpd_query_key_value(_query, "rssithreshold", _valuechar, sizeof(_valuechar)) == ESP_OK)
        {
            ESP_LOGD(TAG, "rssithreshold is found: %s", _valuechar);
            rssithreshold = UrlDecode(std::string(_valuechar));
        }
    }

    FILE *pFile = fopen(NETWORK_CONFIG_FILE, "w");

    text = ";++++++++++++++++++++++++++++++++++\n";
    text += "; AI on the edge - WLAN configuration\n";
    text += "; ssid: Name of WLAN network (mandatory), e.g. \"WLAN-SSID\"\n";
    text += "; password: Password of WLAN network (mandatory), e.g. \"PASSWORD\"\n\n";
    fputs(text.c_str(), pFile);

    if (ssid.length())
    {
        ssid = "ssid = \"" + ssid + "\"\n";
    }
    else
    {
        ssid = "ssid = \"\"\n";
    }
    fputs(ssid.c_str(), pFile);

    if (pwd.length())
    {
        pwd = "password = \"" + pwd + "\"\n";
    }
    else
    {
        pwd = "password = \"\"\n";
    }
    fputs(pwd.c_str(), pFile);

    text = "\n;++++++++++++++++++++++++++++++++++\n";
    text += "; Hostname: Name of device in network\n";
    text += "; This parameter can be configured via WebUI configuration\n";
    text += "; Default: \"watermeter\", if nothing is configured\n\n";
    fputs(text.c_str(), pFile);

    if (hn.length())
    {
        hn = "hostname = \"" + hn + "\"\n";
    }
    else
    {
        hn = ";hostname = \"watermeter\"\n";
    }
    fputs(hn.c_str(), pFile);

    text = "\n;++++++++++++++++++++++++++++++++++\n";
    text += "; Fixed IP: If you like to use fixed IP instead of DHCP (default), the following\n";
    text += "; parameters needs to be configured: ip, gateway, netmask are mandatory, dns optional\n\n";
    fputs(text.c_str(), pFile);

    if (ip.length())
    {
        ip = "ip = \"" + ip + "\"\n";
    }
    else
    {
        ip = ";ip = \"xxx.xxx.xxx.xxx\"\n";
    }
    fputs(ip.c_str(), pFile);

    if (gw.length())
    {
        gw = "gateway = \"" + gw + "\"\n";
    }
    else
    {
        gw = ";gateway = \"xxx.xxx.xxx.xxx\"\n";
    }
    fputs(gw.c_str(), pFile);

    if (nm.length())
    {
        nm = "netmask = \"" + nm + "\"\n";
    }
    else
    {
        nm = ";netmask = \"xxx.xxx.xxx.xxx\"\n";
    }
    fputs(nm.c_str(), pFile);

    text = "\n;++++++++++++++++++++++++++++++++++\n";
    text += "; DNS server (optional, if no DNS is configured, gateway address will be used)\n\n";
    fputs(text.c_str(), pFile);

    if (dns.length())
    {
        dns = "dns = \"" + dns + "\"\n";
    }
    else
    {
        dns = ";dns = \"xxx.xxx.xxx.xxx\"\n";
    }
    fputs(dns.c_str(), pFile);

    text = "\n;++++++++++++++++++++++++++++++++++\n";
    text += "; WIFI Roaming:\n";
    text += "; Network assisted roaming protocol is activated by default\n";
    text += "; AP / mesh system needs to support roaming protocol 802.11k/v\n";
    text += ";\n";
    text += "; Optional feature (usually not neccessary):\n";
    text += "; RSSI Threshold for client requested roaming query (RSSI < RSSIThreshold)\n";
    text += "; Note: This parameter can be configured via WebUI configuration\n";
    text += "; Default: 0 = Disable client requested roaming query\n\n";
    fputs(text.c_str(), pFile);

    if (rssithreshold.length())
    {
        rssithreshold = "RSSIThreshold = " + rssithreshold + "\n";
    }
    else
    {
        rssithreshold = "RSSIThreshold = 0\n";
    }
    fputs(rssithreshold.c_str(), pFile);

    fflush(pFile);
    fclose(pFile);

    std::string string_temp = "ota without parameter - should not be the case!";
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_send(req, string_temp.c_str(), string_temp.length());

    ESP_LOGD(TAG, "end config.ini");

    return ESP_OK;
}

static esp_err_t upload_post_handler(httpd_req_t *req)
{
    printf("Start des Post Handlers\n");
    MakeDir("/sdcard/config");
    MakeDir("/sdcard/firmware");
    MakeDir("/sdcard/html");
    MakeDir("/sdcard/img_tmp");
    MakeDir("/sdcard/log");
    MakeDir("/sdcard/demo");
    printf("Nach Start des Post Handlers\n");

    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "upload_post_handlerAP");
    char filepath[FILE_PATH_MAX];

    const char *filename = get_path_from_uri(filepath, "/sdcard", req->uri + sizeof("/upload") - 1, sizeof(filepath));
    if (!filename)
    {
        httpd_resp_send_err(req, HTTPD_414_URI_TOO_LONG, "Filename too long");
        return ESP_FAIL;
    }

    printf("filepath: %s, filename: %s\n", filepath, filename);
    DeleteFile(std::string(filepath));

    FILE *pFile = fopen(filepath, "w");
    if (!pFile)
    {
        ESP_LOGE(TAG, "Failed to create file: %s", filepath);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to create file");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Receiving file: %s...", filename);

    char buf[1024];
    int received;
    int remaining = req->content_len;

    while (remaining > 0)
    {
        ESP_LOGI(TAG, "Remaining size: %d", remaining);
        if ((received = httpd_req_recv(req, buf, MIN(remaining, 1024))) <= 0)
        {
            if (received == HTTPD_SOCK_ERR_TIMEOUT)
            {
                continue;
            }

            fclose(pFile);
            unlink(filepath);

            ESP_LOGE(TAG, "File reception failed!");
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to receive file");
            return ESP_FAIL;
        }

        if (received && (received != fwrite(buf, 1, received, pFile)))
        {
            fclose(pFile);
            unlink(filepath);

            ESP_LOGE(TAG, "File write failed!");
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to write file to storage");
            return ESP_FAIL;
        }

        remaining -= received;
    }

    fclose(pFile);
    isConfigINI = true;

    pFile = fopen("/sdcard/update.txt", "w");
    std::string file_temp = "/sdcard" + std::string(filename);
    fwrite(file_temp.c_str(), strlen(file_temp.c_str()), 1, pFile);
    fclose(pFile);

    ESP_LOGI(TAG, "File reception complete");
    httpd_resp_set_hdr(req, "Location", "/test");
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/test");
    httpd_resp_send_chunk(req, NULL, 0);

    ESP_LOGI(TAG, "Update page send out");

    return ESP_OK;
}

esp_err_t register_server_ap_uri(httpd_handle_t server)
{
    esp_err_t ret = ESP_OK;

    httpd_uri_t reboot_handle = {
        .uri = "/reboot", // Match all URIs of type /path/to/file
        .method = HTTP_GET,
        .handler = APPLY_BASIC_AUTH_FILTER(reboot_handler),
        .user_ctx = NULL // Pass server data as context
    };
    ret |= httpd_register_uri_handler(server, &reboot_handle);

    httpd_uri_t config_ini_handle = {
        .uri = "/config", // Match all URIs of type /path/to/file
        .method = HTTP_GET,
        .handler = APPLY_BASIC_AUTH_FILTER(config_ini_handler),
        .user_ctx = NULL // Pass server data as context
    };
    ret |= httpd_register_uri_handler(server, &config_ini_handle);

    /* URI handler for uploading files to server */
    httpd_uri_t file_upload = {
        .uri = "/upload/*", // Match all URIs of type /upload/path/to/file
        .method = HTTP_POST,
        .handler = APPLY_BASIC_AUTH_FILTER(upload_post_handler),
        .user_ctx = NULL // Pass server data as context
    };
    ret |= httpd_register_uri_handler(server, &file_upload);

    httpd_uri_t test_uri = {
        .uri = "*",
        .method = HTTP_GET,
        .handler = APPLY_BASIC_AUTH_FILTER(test_handler),
        .user_ctx = NULL};
    ret |= httpd_register_uri_handler(server, &test_uri);

    return ret;
}
