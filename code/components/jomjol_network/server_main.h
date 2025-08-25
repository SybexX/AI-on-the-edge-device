#ifndef SERVER_MAIN_H
#define SERVER_MAIN_H

#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_system.h>
#include <nvs_flash.h>
#include <sys/param.h>
#include <nvs_flash.h>
#include <esp_vfs.h>
#include <esp_netif.h>
#include <esp_eth.h>
#include <esp_http_server.h>

#include "defines.h"
#include "Helper.h"

#include "server_GpioPin.h"
#include "server_GpioHandler.h"

extern httpd_handle_t my_httpd_server;

esp_err_t Init_Network(void);
esp_err_t Init_Webserver(void);

esp_err_t start_webserver(void);

esp_err_t register_server_main_uri(httpd_handle_t my_server);

#endif
