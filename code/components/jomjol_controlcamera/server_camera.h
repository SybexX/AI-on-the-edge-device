#pragma once

#ifndef SERVER_CAMERA_H
#define SERVER_CAMERA_H

#include <esp_log.h>
#include <esp_http_server.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/event_groups.h>

#include "defines.h"
#include "Helper.h"

esp_err_t register_server_camera_uri(httpd_handle_t my_server);

#endif
