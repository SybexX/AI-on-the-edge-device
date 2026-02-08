#pragma once

#ifndef READ_NETWORK_CONFIG_H
#define READ_NETWORK_CONFIG_H

#include <string>

struct network_config_t
{
    std::string ssid = "";
    std::string password = "";
    std::string hostname = "watermeter"; // Default: watermeter
    std::string ipaddress = "";
    std::string gateway = "";
    std::string netmask = "";
    std::string dns = "";
    std::string http_username = "";
    std::string http_password = "";
    int rssi_threshold = 0; // Default: 0 -> ROAMING disabled
};
extern struct network_config_t network_config;

int LoadWlanFromFile(std::string fn);
bool ChangeHostName(std::string fn, std::string _newhostname);
bool ChangeRSSIThreshold(std::string fn, int _newrssithreshold);

#endif // READ_NETWORK_CONFIG_H
