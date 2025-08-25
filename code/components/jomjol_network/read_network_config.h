#pragma once

#ifndef READ_NETWORK_CONFIG_H
#define READ_NETWORK_CONFIG_H

#include <string>
#include <esp_err.h>
#include <esp_log.h>

struct network_config
{
    std::string connection_type = "";
    std::string connection_type_temp = "";

    std::string ssid = "";
    std::string eapid = "";
    std::string username = "";
    std::string password = "";
    std::string hostname = "watermeter"; // Default: watermeter
    std::string ipaddress = "";
    std::string gateway = "";
    std::string netmask = "";
    std::string dns = "";
    int rssi_threshold = 0; // Default: 0 -> ROAMING disabled

    std::string ssid_temp = "";
    std::string eapid_temp = "";
    std::string username_temp = "";
    std::string password_temp = "";
    std::string hostname_temp = "watermeter"; // Default: watermeter
    std::string ipaddress_temp = "";
    std::string gateway_temp = "";
    std::string netmask_temp = "";
    std::string dns_temp = "";
    int rssi_threshold_temp = 0; // Default: 0 -> ROAMING disabled

    bool fix_ipaddress_used = false;
    bool fix_ipaddress_used_temp = false;

    bool http_auth = false;
    std::string http_username = "";
    std::string http_password = "";

    bool http_auth_temp = false;
    std::string http_username_temp = "";
    std::string http_password_temp = "";
};
extern struct network_config network_config;

#define NETWORK_CONNECTION_WIFI_AP_SETUP "Wifi_AP_Setup"
#define NETWORK_CONNECTION_WIFI_AP "Wifi_AP"
#define NETWORK_CONNECTION_WIFI_STA "Wifi_STA"
#if (CONFIG_ETH_ENABLED && CONFIG_ETH_USE_SPI_ETHERNET && CONFIG_ETH_SPI_ETHERNET_W5500)
#define NETWORK_CONNECTION_ETH "Ethernet"
#endif // (CONFIG_ETH_ENABLED && CONFIG_ETH_USE_SPI_ETHERNET && CONFIG_ETH_SPI_ETHERNET_W5500)
#define NETWORK_CONNECTION_DISCONNECT "Disconnect"

std::string *getHostname(void);
std::string *getIPAddress(void);
std::string *getSSID(void);

int LoadNetworkConfigFromFile(std::string filename);

esp_err_t GetAuthWebUIConfig(std::string filename);
esp_err_t ChangeNetworkConfig(std::string filename);

#endif // READ_NETWORK_INI_H
