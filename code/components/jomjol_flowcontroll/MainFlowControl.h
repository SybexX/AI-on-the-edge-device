#pragma once

#ifndef MAINFLOWCONTROL_H
#define MAINFLOWCONTROL_H

#include <esp_log.h>
#include <string>

#include <esp_http_server.h>
#include "CImageBasis.h"
#include "ClassFlowControll.h"
#include "openmetrics.h"

extern ClassFlowControll flowctrl;

bool get_deep_sleep_state(void);
void set_deep_sleep_state(bool _deep_sleep_state);

long get_auto_interval(void);
void set_auto_interval(long _auto_interval);

void CheckIsPlannedReboot(void);
bool getIsPlannedReboot(void);

void InitializeFlowTask(void);
void DeleteMainFlowTask(void);
bool isSetupModusActive(void);

int getCountFlowRounds(void);

#ifdef ENABLE_MQTT
esp_err_t MQTTCtrlFlowStart(std::string _topic);
#endif // ENABLE_MQTT

esp_err_t GetRawJPG(httpd_req_t *req);
esp_err_t GetJPG(std::string _filename, httpd_req_t *req);

esp_err_t register_server_main_flow_uri(httpd_handle_t my_server);

#endif // MAINFLOWCONTROL_H
