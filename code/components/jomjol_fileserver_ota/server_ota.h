#pragma once

#ifndef SERVEROTA_H
#define SERVEROTA_H

#include <esp_log.h>
#include <esp_http_server.h>
#include <string>

void checkOTAUpdate(void);
void doReboot(void);
void doRebootOTA(void);
void hard_restart(void);
void checkUpdate(void);

esp_err_t register_server_ota_uri(httpd_handle_t my_server);

#endif // SERVEROTA_H
