#pragma once

#ifndef SERVER_GPIOPIN_H
#define SERVER_GPIOPIN_H

#include <map>
#include <driver/gpio.h>
#include <driver/ledc.h>
#include <esp_log.h>
#include <esp_http_server.h>

#include "SmartLeds.h"

#include "ClassControllCamera.h"

#include "Helper.h"
#include "defines.h"

typedef enum
{
    GPIO_PIN_MODE_DISABLED = 0x0,
    GPIO_PIN_MODE_INPUT = 0x1,
    GPIO_PIN_MODE_INPUT_PULLUP = 0x2,
    GPIO_PIN_MODE_INPUT_PULLDOWN = 0x3,
    GPIO_PIN_MODE_OUTPUT = 0x4,
    GPIO_PIN_MODE_BUILT_IN_FLASH_LED = 0x5,
    GPIO_PIN_MODE_OUTPUT_PWM = 0x6,
    GPIO_PIN_MODE_EXTERNAL_FLASH_PWM = 0x7,
    GPIO_PIN_MODE_EXTERNAL_FLASH_WS281X = 0x8,
} config_gpio_pin_mode_t;

struct ConfigGpioResult
{
    gpio_num_t gpio;
    int value;
};

typedef enum
{
    GPIO_SET_SOURCE_INTERNAL = 0,
    GPIO_SET_SOURCE_MQTT = 1,
    GPIO_SET_SOURCE_HTTP = 2,
} config_gpio_set_source_t;

class ConfigGpioPin
{
private:
    gpio_num_t _gpio;
    const char *_name;
    config_gpio_pin_mode_t _mode;
    gpio_int_type_t _interruptType;
    std::string _mqttTopic;
    int currentState = -1;

public:
    ConfigGpioPin(gpio_num_t gpio, const char *name, config_gpio_pin_mode_t mode, gpio_int_type_t interruptType, uint8_t dutyResolution, std::string mqttTopic, bool httpEnable);
    ~ConfigGpioPin();

    void init();
    bool getValue(std::string *errorText);
    void setValue(bool value, config_gpio_set_source_t setSource, std::string *errorText);
#ifdef ENABLE_MQTT
    bool handleMQTT(std::string, char *data, int data_len);
#endif // ENABLE_MQTT
    void publishState();
    void gpioInterrupt(int value);

    gpio_int_type_t getInterruptType() { return _interruptType; }
    config_gpio_pin_mode_t getMode() { return _mode; }
    gpio_num_t getGPIO() { return _gpio; };
};

#endif // SERVER_GPIOPIN_H
