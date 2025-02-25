#pragma once

#ifndef SERVEROTA_H
#define SERVEROTA_H

#include <string>
#include <stdio.h>
#include <string.h>
#include <sys/errno.h>
#include <sys/param.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <esp_spiffs.h>
#include <esp_vfs.h>

#include <esp_http_server.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "ClassLogFile.h"
#include "basic_auth.h"

#include "Helper.h"
#include "../../include/defines.h"

void CheckUpdate(void);

#ifdef CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
void CheckOTAPartition(void);
#endif

void doReboot(void);
void doRebootOTA(void);
void hard_restart(void);

void register_server_ota_sdcard_uri(httpd_handle_t server);

#endif // SERVEROTA_H
