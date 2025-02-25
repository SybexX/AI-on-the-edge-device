#pragma once

#ifndef SERVERHELP_H
#define SERVERHELP_H

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

const char *get_path_from_uri(char *dest, const char *base_path, const char *uri, size_t destsize);
esp_err_t send_file(httpd_req_t *req, std::string filename);
esp_err_t set_content_type_from_file(httpd_req_t *req, const char *filename);

void delete_all_in_directory(std::string _directory);
void delete_all_file_in_directory(std::string _directory);

std::string unzip_file(std::string _in_zip_file, std::string _target_directory);
std::string unzip_ota(std::string _in_zip_file, std::string _target_directory);

#endif // SERVERHELP_H
