#pragma once

#ifndef SERVER_CONFIG_H
#define SERVER_CONFIG_H

#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_system.h>
#include <nvs_flash.h>
#include <sys/param.h>
#include <nvs_flash.h>
#include <esp_netif.h>
#include <esp_eth.h>
#include <esp_http_server.h>

#include "server_GpioPin.h"
#include "server_GpioHandler.h"

extern httpd_handle_t my_server_ap;

esp_err_t Init_Server_Config(void);

esp_err_t start_webserverAP(void);
esp_err_t register_server_ap_uri(httpd_handle_t server);

#endif // SERVER_CONFIG_H
