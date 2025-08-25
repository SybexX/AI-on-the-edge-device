/*  WiFi softAP Example
   This example code is in the Public Domain (or CC0 licensed, at your option.)
   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#pragma once

#ifndef CONNECT_WIFI_AP_H
#define CONNECT_WIFI_AP_H

#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_system.h>
#include <nvs_flash.h>
#include <sys/param.h>
#include <nvs_flash.h>
#include <esp_netif.h>
#include <esp_eth.h>
#include <esp_tls_crypto.h>
#include <esp_http_server.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/event_groups.h>

#include "defines.h"
#include "Helper.h"

esp_err_t wifi_init_ap(void);
void wifi_deinit_ap(void);

bool getWIFIAPisConnected(void);

#endif // CONNECT_WIFI_AP_H
