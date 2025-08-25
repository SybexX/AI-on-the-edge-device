#pragma once

#ifndef SERVERFILE_H
#define SERVERFILE_H

#include <string>
#include <esp_vfs.h>
#include <esp_http_server.h>

#include "defines.h"
#include "Helper.h"

typedef struct rest_server_context
{
    char base_path[ESP_VFS_PATH_MAX + 1];
    char scratch[SERVER_FILE_SCRATCH_BUFSIZE];
} rest_server_context_t;
extern rest_server_context_t *rest_context;

esp_err_t get_number_list_handler(httpd_req_t *req);
esp_err_t get_data_list_handler(httpd_req_t *req);
esp_err_t get_tflite_list_handler(httpd_req_t *req);
esp_err_t get_config_list_handler(httpd_req_t *req);

void unzip_file(std::string _in_zip_file, std::string _target_directory);
std::string unzip_ota_file(std::string _in_zip_file, std::string _html_tmp, std::string _html_final, std::string _target_bin, std::string _main = "/sdcard/", bool _initial_setup = false);

esp_err_t register_server_file_uri(const char *base_path, httpd_handle_t my_server);

#endif // SERVERFILE_H
