#pragma once

#ifndef SERVEROTA_H
#define SERVEROTA_H

#include <string>
#include <esp_log.h>
#include <esp_http_server.h>

void CheckUpdate();
void CheckOTAUpdate();

void doReboot();
void doRebootOTA();
void hard_restart();

void register_server_ota_sdcard_uri(httpd_handle_t server);

#endif //SERVEROTA_H