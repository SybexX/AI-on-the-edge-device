#pragma once

#ifndef SERVER_GPIOHANDLER_H
#define SERVER_GPIOHANDLER_H

#include "server_GpioPin.h"

#include <map>
#include "driver/gpio.h"
#include "driver/ledc.h"
#include <esp_log.h>
#include <esp_http_server.h>

#include "SmartLeds.h"

#include "Helper.h"
#include "defines.h"

class ConfigGpioHandler
{
private:
    std::string _configFile;
    httpd_handle_t _httpServer;
    std::map<gpio_num_t, ConfigGpioPin *> *gpioMap = NULL;
    TaskHandle_t xHandleTaskGpio = NULL;
    bool _isEnabled = false;

    int LEDNumbers = 2;
    Rgb LEDColor = Rgb{255, 255, 255};
    LedType LEDType = LED_WS2812;
#ifdef __LEDGLOBAL
    SmartLed *leds_global = NULL;
#endif

    bool readConfig();
    void clear();

    gpio_num_t resolvePinNr(uint8_t pinNr);
    config_gpio_pin_mode_t resolvePinMode(std::string input);
    gpio_int_type_t resolveIntType(std::string input);

public:
    ConfigGpioHandler(std::string configFile, httpd_handle_t httpServer);
    ~ConfigGpioHandler();

    void init();
    void deinit();
    void registerGpioUri();
    esp_err_t handleHttpRequest(httpd_req_t *req);
    void taskHandler();
    void gpioInterrupt(ConfigGpioResult *gpioResult);
    void flashLightEnable(bool value);
    bool isEnabled() { return _isEnabled; }
#ifdef ENABLE_MQTT
    void handleMQTTconnect();
#endif // ENABLE_MQTT
};

ConfigGpioHandler *gpio_handler_get();

esp_err_t callHandleHttpRequest(httpd_req_t *req);
void taskGpioHandler(void *pvParameter);

void gpio_handler_create(httpd_handle_t server);
void gpio_handler_init();
void gpio_handler_deinit();
void gpio_handler_destroy();

#endif // SERVER_GPIOHANDLER_H
