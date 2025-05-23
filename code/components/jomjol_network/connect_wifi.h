#pragma once

#ifndef CONNECT_WIFI_H
#define CONNECT_WIFI_H

#include <string>

int wifi_init_sta(void);
std::string *getIPAddress();
std::string *getSSID();
int get_WIFI_RSSI();
std::string *getHostname();

bool getWIFIisConnected();
void WIFIDestroy();

#if (defined WIFI_USE_MESH_ROAMING && defined WIFI_USE_MESH_ROAMING_ACTIVATE_CLIENT_TRIGGERED_QUERIES)
void wifiRoamingQuery(void);
#endif

#ifdef WIFI_USE_ROAMING_BY_SCANNING
void wifiRoamByScanning(void);
#endif

#endif // CONNECT_WIFI_H
