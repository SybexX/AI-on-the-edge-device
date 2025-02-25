#pragma once

#ifndef SERVERFILE_H
#define SERVERFILE_H

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

void register_server_file_uri(httpd_handle_t server, const char *base_path);

esp_err_t get_data_file_handler(httpd_req_t *req);
esp_err_t get_tflite_file_handler(httpd_req_t *req);
esp_err_t get_config_file_handler(httpd_req_t *req);

esp_err_t get_numbers_file_handler(httpd_req_t *req);

#endif // SERVERFILE_H
