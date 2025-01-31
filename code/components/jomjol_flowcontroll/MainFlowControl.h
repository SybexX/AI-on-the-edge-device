#pragma once

#ifndef MAINFLOWCONTROL_H
#define MAINFLOWCONTROL_H

#include <string>
#include <esp_log.h>

#include <esp_http_server.h>

#include "ClassFlowControll.h"

#include "../../include/defines.h"

typedef struct
{
    cam_config_t CamConfig;

    bool CameraInitSuccessful;
    bool CameraInitAFSuccessful;

    bool DemoMode;
    bool SaveAllFiles;
} camera_flow_config_temp_t;
extern camera_flow_config_temp_t CFstatus;

extern ClassFlowControll flowctrl;

void register_server_main_flow_task_uri(httpd_handle_t server);

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

#endif // MAINFLOWCONTROL_H
