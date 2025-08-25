#include "read_network_config.h"

#include <fstream>
#include <string>
#include <vector>
#include <sstream>
#include <iostream>
#include <string.h>
#include <esp_log.h>

#include "ClassLogFile.h"

#include "defines.h"
#include "Helper.h"

static const char *TAG = "NETWORK INI";

struct network_config network_config = {};

std::string *getHostname(void)
{
    return &network_config.hostname;
}

std::string *getIPAddress(void)
{
    return &network_config.ipaddress;
}

std::string *getSSID(void)
{
    return &network_config.ssid;
}

int LoadNetworkConfigFromFile(std::string filename)
{
    std::string line = "";
    std::vector<std::string> splitted;

    filename = FormatFileName(filename);
    FILE *pFile = fopen(filename.c_str(), "r");
    if (pFile == NULL)
    {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Unable to open file (read). Device init aborted!");
        return -1;
    }

    ESP_LOGD(TAG, "LoadNetworkFromFile: network.ini opened");

    char temp_bufer[256];
    if (fgets(temp_bufer, sizeof(temp_bufer), pFile) == NULL)
    {
        line = "";
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "file opened, but empty or content not readable. Device init aborted!");
        fclose(pFile);
        return -1;
    }
    else
    {
        line = std::string(temp_bufer);
    }

    int _fix_ipaddress = 3;

    while ((line.size() > 0) || !(feof(pFile)))
    {
        // Skip lines which starts with ';'
        if (line[0] != ';')
        {
            splitted = splitLine(line);

            if (splitted.size() > 1)
            {
                std::string _param = toUpper(splitted[0]);
                if (_param == "CONNECTIONTYPE")
                {
                    network_config.connection_type = splitted[1];
                    LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Connection Type: " + network_config.connection_type);
                }
                else if (_param == "SSID")
                {
                    network_config.ssid = splitted[1];
                    LogFile.WriteToFile(ESP_LOG_INFO, TAG, "SSID: " + network_config.ssid);
                }
                else if ((splitted.size() > 1) && (toUpper(splitted[0]) == "EAPID"))
                {
                    network_config.eapid = splitted[1];
                    LogFile.WriteToFile(ESP_LOG_INFO, TAG, "EPA-ID: " + network_config.eapid);
                }
                else if ((splitted.size() > 1) && (toUpper(splitted[0]) == "USERNAME"))
                {
                    network_config.username = splitted[1];
                    LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Username: " + network_config.username);
                }
                else if (_param == "PASSWORD")
                {
                    network_config.password = splitted[1];
#ifndef __HIDE_PASSWORD
                    LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Password: " + network_config.password);
#else
                    if (network_config.password.empty())
                    {
                        LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Password: wifi_pw_not_set");
                    }
                    else
                    {
                        LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Password: wifi_pw_hide");
                        // LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Password: XXXXXXXX");
                    }
#endif
                }
                else if (_param == "HOSTNAME")
                {
                    network_config.hostname = splitted[1];
                    LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Hostname: " + network_config.hostname);
                }
                else if (_param == "IP")
                {
                    network_config.ipaddress = splitted[1];
                    _fix_ipaddress--;
                    LogFile.WriteToFile(ESP_LOG_INFO, TAG, "IP-Address: " + network_config.ipaddress);
                }
                else if (_param == "GATEWAY")
                {
                    network_config.gateway = splitted[1];
                    _fix_ipaddress--;
                    LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Gateway: " + network_config.gateway);
                }
                else if (_param == "NETMASK")
                {
                    network_config.netmask = splitted[1];
                    _fix_ipaddress--;
                    LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Netmask: " + network_config.netmask);
                }
                else if (_param == "DNS")
                {
                    network_config.dns = splitted[1];
                    LogFile.WriteToFile(ESP_LOG_INFO, TAG, "DNS: " + network_config.dns);
                }
                else if (_param == "HTTP_AUTH")
                {
                    network_config.http_auth = alphanumericToBoolean(splitted[1]);
                    if (network_config.http_auth)
                    {
                        LogFile.WriteToFile(ESP_LOG_INFO, TAG, "HTTP_AUTH: enabled");
                    }
                    else
                    {
                        LogFile.WriteToFile(ESP_LOG_INFO, TAG, "HTTP_AUTH: disabled");
                    }
                }
                else if (_param == "HTTP_USERNAME")
                {
                    network_config.http_username = splitted[1];
                    LogFile.WriteToFile(ESP_LOG_INFO, TAG, "HTTP_USERNAME: " + network_config.http_username);
                }
                else if (_param == "HTTP_PASSWORD")
                {
                    network_config.http_password = splitted[1];
#ifndef __HIDE_PASSWORD
                    LogFile.WriteToFile(ESP_LOG_INFO, TAG, "HTTP_PASSWORD: " + network_config.http_password);
#else
                    if (network_config.http_password.empty())
                    {
                        LogFile.WriteToFile(ESP_LOG_INFO, TAG, "HTTP_PASSWORD: http_pw_not_set");
                    }
                    else
                    {
                        LogFile.WriteToFile(ESP_LOG_INFO, TAG, "HTTP_PASSWORD: http_pw_hide");
                    }
#endif
                }

#if (defined WLAN_USE_ROAMING_BY_SCANNING || (defined WLAN_USE_MESH_ROAMING && defined WLAN_USE_MESH_ROAMING_ACTIVATE_CLIENT_TRIGGERED_QUERIES))
                else if (_param == "RSSITHRESHOLD")
                {
                    network_config.rssi_threshold = atoi(splitted[1].c_str());
                    LogFile.WriteToFile(ESP_LOG_INFO, TAG, "RSSIThreshold: " + std::to_string(network_config.rssi_threshold));
                }
#endif
            }
        }

        /* read next line */
        if (fgets(temp_bufer, sizeof(temp_bufer), pFile) == NULL)
        {
            line = "";
        }
        else
        {
            line = std::string(temp_bufer);
        }
    }

    fclose(pFile);

    /* Check if SSID is empty (mandatory parameter) */
#if (CONFIG_ETH_ENABLED && CONFIG_ETH_USE_SPI_ETHERNET && CONFIG_ETH_SPI_ETHERNET_W5500)
    if ((network_config.connection_type.empty()) || ((network_config.connection_type != NETWORK_CONNECTION_WIFI_AP_SETUP) && (network_config.connection_type != NETWORK_CONNECTION_WIFI_AP) &&
                                                     (network_config.connection_type != NETWORK_CONNECTION_WIFI_STA) && (network_config.connection_type != NETWORK_CONNECTION_ETH) && (network_config.connection_type != NETWORK_CONNECTION_DISCONNECT)))
#else
    if ((network_config.connection_type.empty()) || ((network_config.connection_type != NETWORK_CONNECTION_WIFI_AP_SETUP) && (network_config.connection_type != NETWORK_CONNECTION_WIFI_AP) &&
                                                     (network_config.connection_type != NETWORK_CONNECTION_WIFI_STA) && (network_config.connection_type != NETWORK_CONNECTION_DISCONNECT)))
#endif
    {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Connection Type empty. It set to WiFi AP setup!");
        network_config.connection_type = NETWORK_CONNECTION_WIFI_AP_SETUP;
    }

    /* Check if SSID is empty (mandatory parameter) */
    if (network_config.ssid.empty() && network_config.connection_type == NETWORK_CONNECTION_WIFI_STA)
    {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "SSID empty. Device init aborted!");
        return -2;
    }

    if (_fix_ipaddress == 0)
    {
        network_config.fix_ipaddress_used = true;
        LogFile.WriteToFile(ESP_LOG_INFO, TAG, "fix ipaddress used");
    }
    else
    {
        network_config.fix_ipaddress_used = false;
    }

    // wenn die Passwörter auf der SD nicht verschlüsselt sind,
    // dann werden sie verschlüsselt auf die SD geschrieben
    EncryptDecryptPwOnSD(true, NETWORK_CONFIG_FILE);

    return 0;
}

esp_err_t GetAuthWebUIConfig(std::string filename)
{
    filename = FormatFileName(filename);
    FILE *pFile = fopen(filename.c_str(), "r");

    if (pFile == NULL)
    {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "GetAuthWebUIConfig: Unable to open file network.ini (read)");
        return ESP_FAIL;
    }

    ESP_LOGD(TAG, "GetAuthWebUIConfig: network.ini opened");

    char temp_bufer[256];
    std::string line = "";

    if (fgets(temp_bufer, sizeof(temp_bufer), pFile) == NULL)
    {
        line = "";
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "GetAuthWebUIConfig: File network.ini opened, but empty or content not readable");
        fclose(pFile);
        return ESP_FAIL;
    }
    else
    {
        line = std::string(temp_bufer);
    }

    std::vector<string> splitted;

    while ((line.size() > 0) || !(feof(pFile)))
    {
        splitted = splitLine(line);

        if (splitted.size() > 1)
        {
            std::string _param = toUpper(splitted[0]);

            if ((_param == "HTTP_AUTH") || (_param == ";HTTP_AUTH"))
            {
                network_config.http_auth = alphanumericToBoolean(splitted[1]);
            }
            else if ((_param == "HTTP_USERNAME") || (_param == ";HTTP_USERNAME"))
            {
                network_config.http_username = splitted[1];
            }
            else if ((_param == "HTTP_PASSWORD") || (_param == ";HTTP_PASSWORD"))
            {
                network_config.http_password = splitted[1];
            }
        }

        /* read next line */
        if (fgets(temp_bufer, sizeof(temp_bufer), pFile) == NULL)
        {
            line = "";
        }
        else
        {
            line = std::string(temp_bufer);
        }
    }

    fclose(pFile);

    return ESP_OK;
}

// Wlan-Einstellungen auf die SD schreiben
esp_err_t ChangeNetworkConfig(std::string filename)
{
    filename = FormatFileName(filename);
    FILE *pFile = fopen(filename.c_str(), "r");

    if (pFile == NULL)
    {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "ChangeNetworkConfigs: Unable to open file network.ini (read)");
        fclose(pFile);
        return ESP_FAIL;
    }

    ESP_LOGD(TAG, "ChangeNetworkConfigs: network.ini opened");

    char temp_bufer[256];
    std::string line = "";

    if (fgets(temp_bufer, sizeof(temp_bufer), pFile) == NULL)
    {
        line = "";
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "ChangeNetworkConfigs: File network.ini opened, but empty or content not readable");
        fclose(pFile);
        return ESP_FAIL;
    }
    else
    {
        line = std::string(temp_bufer);
    }

    std::vector<string> splitted;
    std::vector<string> neuesfile;

    while ((line.size() > 0) || !(feof(pFile)))
    {
        splitted = splitLine(line);

        if (splitted.size() > 1)
        {
            std::string _param = toUpper(splitted[0]);

            ////////////////////////////////////////////////////////////////////
            if ((_param == "CONNECTIONTYPE") || (_param == ";CONNECTIONTYPE"))
            {
                if (network_config.connection_type_temp != network_config.connection_type)
                {
                    if ((network_config.connection_type_temp != "") &&
                        ((network_config.connection_type_temp == "Wifi_AP_Setup") || (network_config.connection_type_temp == "Wifi_AP") || (network_config.connection_type_temp == "Wifi_STA") || (network_config.connection_type_temp == "Ethernet")))
                    {
                        line = "ConnectionType = \"" + network_config.connection_type_temp + "\"\n";
                    }
                    else
                    {
                        line = "ConnectionType = \"Wifi_STA\"\n";
                    }
                }
            }
            ////////////////////////////////////////////////////////////////////
            else if ((_param == "SSID") || (_param == ";SSID"))
            {
                if (network_config.ssid_temp != network_config.ssid)
                {
                    if (network_config.ssid_temp != "")
                    {
                        line = "ssid = \"" + network_config.ssid_temp + "\"\n";
                    }
                }
            }
            ////////////////////////////////////////////////////////////////////
            else if ((_param == "EAPID") || (_param == ";EAPID"))
            {
                if (network_config.eapid_temp != network_config.eapid)
                {
                    if (network_config.eapid_temp != "")
                    {
                        line = "eapid = \"" + network_config.eapid_temp + "\"\n";
                    }
                }
            }
            ////////////////////////////////////////////////////////////////////
            else if ((_param == "USERNAME") || (_param == ";USERNAME"))
            {
                if (network_config.username_temp != network_config.username)
                {
                    if (network_config.username_temp != "")
                    {
                        line = "username = \"" + network_config.username_temp + "\"\n";
                    }
                }
            }
            ////////////////////////////////////////////////////////////////////
            else if ((_param == "PASSWORD") || (_param == ";PASSWORD"))
            {
                if (network_config.password_temp != network_config.password)
                {
                    if (network_config.password_temp != "")
                    {
                        line = "password = \"" + EncryptPwString(network_config.password_temp) + "\"\n";
                    }
                    else
                    {
                        line = "password = \"\"\n";
                    }
                }
            }
            ////////////////////////////////////////////////////////////////////
            else if ((_param == "HOSTNAME") || (_param == ";HOSTNAME"))
            {
                if (network_config.hostname_temp != network_config.hostname)
                {
                    if (network_config.hostname_temp != "")
                    {
                        line = "hostname = \"" + network_config.hostname_temp + "\"\n";
                    }
                    else
                    {
                        line = ";hostname = \"watermeter\"\n";
                    }
                }
            }
            ////////////////////////////////////////////////////////////////////
            else if ((_param == "IP") || (_param == ";IP"))
            {
                if (network_config.ipaddress_temp != network_config.ipaddress)
                {
                    if (network_config.ipaddress_temp != "")
                    {
                        line = "ip = \"" + network_config.ipaddress_temp + "\"\n";
                    }
                    else
                    {
                        line = ";ip = \"xxx.xxx.xxx.xxx\"\n";
                    }
                }
            }
            ////////////////////////////////////////////////////////////////////
            else if ((_param == "GATEWAY") || (_param == ";GATEWAY"))
            {
                if (network_config.gateway_temp != network_config.gateway)
                {
                    if (network_config.gateway_temp != "")
                    {
                        line = "gateway = \"" + network_config.gateway_temp + "\"\n";
                    }
                    else
                    {
                        line = ";gateway = \"xxx.xxx.xxx.xxx\"\n";
                    }
                }
            }
            ////////////////////////////////////////////////////////////////////
            else if ((_param == "NETMASK") || (_param == ";NETMASK"))
            {
                if (network_config.netmask_temp != network_config.netmask)
                {
                    if (network_config.netmask_temp != "")
                    {
                        line = "netmask = \"" + network_config.netmask_temp + "\"\n";
                    }
                    else
                    {
                        line = ";netmask = \"xxx.xxx.xxx.xxx\"\n";
                    }
                }
            }
            ////////////////////////////////////////////////////////////////////
            else if ((_param == "DNS") || (_param == ";DNS"))
            {
                if (network_config.dns_temp != network_config.dns)
                {
                    if (network_config.dns_temp != "")
                    {
                        line = "dns = \"" + network_config.dns_temp + "\"\n";
                    }
                    else
                    {
                        line = ";dns = \"xxx.xxx.xxx.xxx\"\n";
                    }
                }
            }
            ////////////////////////////////////////////////////////////////////
            else if ((_param == "RSSITHRESHOLD") || (_param == ";RSSITHRESHOLD"))
            {
                if (network_config.rssi_threshold_temp != network_config.rssi_threshold)
                {
                    if ((network_config.rssi_threshold_temp <= 0) && (network_config.rssi_threshold_temp >= (-100)))
                    {
                        line = "RSSIThreshold = \"" + to_string(network_config.rssi_threshold_temp) + "\"\n";
                    }
                    else
                    {
                        line = ";RSSIThreshold = \"0\"\n";
                    }
                }
            }
            ////////////////////////////////////////////////////////////////////
            else if ((_param == "HTTP_AUTH") || (_param == ";HTTP_AUTH"))
            {
                if (network_config.http_auth_temp != network_config.http_auth)
                {
                    if (network_config.http_auth_temp)
                    {
                        line = "http_auth = \"true\"\n";
                    }
                    else
                    {
                        line = "http_auth = \"false\"\n";
                    }
                }
            }
            ////////////////////////////////////////////////////////////////////
            else if ((_param == "HTTP_USERNAME") || (_param == ";HTTP_USERNAME"))
            {
                if (network_config.http_username_temp != network_config.http_username)
                {
                    if (network_config.http_username_temp != "")
                    {
                        line = "http_username = \"" + network_config.http_username_temp + "\"\n";
                    }
                    else
                    {
                        line = "http_username = \"admin\"\n";
                    }
                }
            }
            ////////////////////////////////////////////////////////////////////
            else if ((_param == "HTTP_PASSWORD") || (_param == ";HTTP_PASSWORD"))
            {
                if (network_config.http_password_temp != network_config.http_password)
                {
                    if (network_config.http_password_temp != "")
                    {
                        line = "http_password = \"" + EncryptPwString(network_config.http_password_temp) + "\"\n";
                    }
                    else
                    {
                        line = "http_password = \"admin\"\n";
                    }
                }
            }
        }

        neuesfile.push_back(line);

        if (fgets(temp_bufer, sizeof(temp_bufer), pFile) == NULL)
        {
            line = "";
        }
        else
        {
            line = std::string(temp_bufer);
        }
    }

    fclose(pFile);

    pFile = fopen(filename.c_str(), "w+");
    if (pFile == NULL)
    {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "ChangeNetworkConfigs: Unable to open file network.ini (write)");
        fclose(pFile);
        return ESP_FAIL;
    }

    for (int i = 0; i < neuesfile.size(); ++i)
    {
        fputs(neuesfile[i].c_str(), pFile);
    }

    fclose(pFile);
    ESP_LOGD(TAG, "ChangeNetworkConfigs done");

    return ESP_OK;
}
