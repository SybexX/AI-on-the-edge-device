#include <string>
#include <string.h>
#include <vector>
#include <functional>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"

#include <sys/stat.h>
#include "esp_check.h"

#include "onewire_bus.h"
#include "ds18b20.h"

#include "server_GpioHandler.h"

#include "ClassLogFile.h"
#include "configFile.h"

#ifdef ENABLE_MQTT
#include "interface_mqtt.h"
#include "server_mqtt.h"
#endif // ENABLE_MQTT

#include "basic_auth.h"

static const char *TAG = "GPIOHANDLER";

static ConfigGpioHandler *gpioHandler = NULL;
QueueHandle_t gpio_queue_handle = NULL;

static void gpioHandlerTask(void *arg)
{
    ESP_LOGD(TAG, "start interrupt task");
    while (1)
    {
        if (uxQueueMessagesWaiting(gpio_queue_handle))
        {
            while (uxQueueMessagesWaiting(gpio_queue_handle))
            {
                ConfigGpioResult gpioResult;
                xQueueReceive(gpio_queue_handle, (void *)&gpioResult, 10);
                ESP_LOGD(TAG, "gpio: %d state: %d", gpioResult.gpio, gpioResult.value);
                ((ConfigGpioHandler *)arg)->gpioInterrupt(&gpioResult);
            }
        }

        ((ConfigGpioHandler *)arg)->taskHandler();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

ConfigGpioHandler::ConfigGpioHandler(std::string configFile, httpd_handle_t httpServer)
{
    ESP_LOGI(TAG, "start GpioHandler");
    _configFile = configFile;
    _httpServer = httpServer;

    ESP_LOGI(TAG, "register GPIO Uri");
    registerGpioUri();
}

ConfigGpioHandler::~ConfigGpioHandler()
{
    if (gpioMap != NULL)
    {
        clear();
        delete gpioMap;
    }
}

void ConfigGpioHandler::init()
{
    ESP_LOGD(TAG, "*************** Start GPIOHandler_Init *****************");

    if (gpioMap == NULL)
    {
        gpioMap = new std::map<gpio_num_t, ConfigGpioPin *>();
    }
    else
    {
        clear();
    }

    ESP_LOGI(TAG, "read GPIO config and init GPIO");
    if (!readConfig())
    {
        clear();
        delete gpioMap;
        gpioMap = NULL;
        ESP_LOGI(TAG, "GPIO init completed, handler is disabled");
        return;
    }

    for (std::map<gpio_num_t, ConfigGpioPin *>::iterator it = gpioMap->begin(); it != gpioMap->end(); ++it)
    {
        it->second->init();
    }

#ifdef ENABLE_MQTT
    std::function<void()> f = std::bind(&ConfigGpioHandler::handleMQTTconnect, this);
    MQTTregisterConnectFunction("gpio-handler", f);
#endif // ENABLE_MQTT

    if (xHandleTaskGpio == NULL)
    {
        gpio_queue_handle = xQueueCreate(10, sizeof(ConfigGpioResult));
        BaseType_t xReturned = xTaskCreate(&gpioHandlerTask, "gpio_int", 3 * 1024, (void *)this, tskIDLE_PRIORITY + 4, &xHandleTaskGpio);
        if (xReturned == pdPASS)
        {
            ESP_LOGD(TAG, "xHandletaskGpioHandler started");
        }
        else
        {
            ESP_LOGD(TAG, "xHandletaskGpioHandler not started %d ", (int)xHandleTaskGpio);
        }
    }

    ESP_LOGI(TAG, "GPIO init completed, is enabled");
}

void ConfigGpioHandler::taskHandler()
{
    if (gpioMap != NULL)
    {
        for (std::map<gpio_num_t, ConfigGpioPin *>::iterator it = gpioMap->begin(); it != gpioMap->end(); ++it)
        {
            if ((it->second->getInterruptType() == GPIO_INTR_DISABLE))
            {
                it->second->publishState();
            }
        }
    }
}

#ifdef ENABLE_MQTT
void ConfigGpioHandler::handleMQTTconnect()
{
    if (gpioMap != NULL)
    {
        for (std::map<gpio_num_t, ConfigGpioPin *>::iterator it = gpioMap->begin(); it != gpioMap->end(); ++it)
        {
            if ((it->second->getMode() == GPIO_PIN_MODE_INPUT) || (it->second->getMode() == GPIO_PIN_MODE_INPUT_PULLDOWN) || (it->second->getMode() == GPIO_PIN_MODE_INPUT_PULLUP))
            {
                it->second->publishState();
            }
        }
    }
}
#endif // ENABLE_MQTT

void ConfigGpioHandler::deinit()
{
#ifdef ENABLE_MQTT
    MQTTunregisterConnectFunction("gpio-handler");
#endif // ENABLE_MQTT

    clear();

    if (xHandleTaskGpio != NULL)
    {
        vTaskDelete(xHandleTaskGpio);
        xHandleTaskGpio = NULL;
    }
}

void ConfigGpioHandler::gpioInterrupt(ConfigGpioResult *gpioResult)
{
    if ((gpioMap != NULL) && (gpioMap->find(gpioResult->gpio) != gpioMap->end()))
    {
        (*gpioMap)[gpioResult->gpio]->gpioInterrupt(gpioResult->value);
    }
}

bool ConfigGpioHandler::readConfig()
{
    if (!gpioMap->empty())
    {
        clear();
    }

    ConfigFile configFile = ConfigFile(_configFile);

    std::vector<std::string> splitted;
    std::string line = "";
    bool disabledLine = false;
    bool eof = false;
    gpio_num_t gpioExtLED = (gpio_num_t)0;

    while ((!configFile.GetNextParagraph(line, disabledLine, eof) || (line.compare("[GPIO]") != 0)) && !eof)
    {
    }
    if (eof)
    {
        return false;
    }

    _isEnabled = !disabledLine;

    if (!_isEnabled)
    {
        return false;
    }

#ifdef ENABLE_MQTT
    // std::string mainTopicMQTT = "";
    std::string mainTopicMQTT = mqttServer_getMainTopic();
    if (mainTopicMQTT.length() > 0)
    {
        mainTopicMQTT = mainTopicMQTT + "/GPIO";
        ESP_LOGD(TAG, "MAINTOPICMQTT found");
    }
#endif // ENABLE_MQTT
    bool registerISR = false;
    while (configFile.getNextLine(&line, disabledLine, eof) && !configFile.isNewParagraph(line))
    {
        splitted = split_line(line);
        ESP_LOGD(TAG, "conf param %s", toUpper(splitted[0]).c_str());
        if (toUpper(splitted[0]) == "MAINTOPICMQTT")
        {
            // ESP_LOGD(TAG, "MAINTOPICMQTT found");
            // mainTopicMQTT = splitted[1];
        }
        else if ((splitted[0].rfind("IO", 0) == 0) && (splitted.size() >= 6))
        {
            ESP_LOGI(TAG, "Enable GP%s in %s mode", splitted[0].c_str(), splitted[1].c_str());
            std::string gpioStr = splitted[0].substr(2, 2);
            gpio_num_t gpioNr = (gpio_num_t)atoi(gpioStr.c_str());
            config_gpio_pin_mode_t pinMode = resolvePinMode(toLower(splitted[1]));
            gpio_int_type_t intType = resolveIntType(toLower(splitted[2]));
            uint16_t dutyResolution = (uint8_t)atoi(splitted[3].c_str());
#ifdef ENABLE_MQTT
            bool mqttEnabled = (toLower(splitted[4]) == "true");
#endif // ENABLE_MQTT
            bool httpEnabled = (toLower(splitted[5]) == "true");
            char gpioName[100];

            if (splitted.size() >= 7)
            {
                strcpy(gpioName, trim(splitted[6]).c_str());
            }
            else
            {
                sprintf(gpioName, "GPIO%d", gpioNr);
            }

#ifdef ENABLE_MQTT
            std::string mqttTopic = mqttEnabled ? (mainTopicMQTT + "/" + gpioName) : "";
#else  // ENABLE_MQTT
            std::string mqttTopic = "";
#endif // ENABLE_MQTT

            ConfigGpioPin *config_gpio_pin = new ConfigGpioPin(gpioNr, gpioName, pinMode, intType, dutyResolution, mqttTopic, httpEnabled);
            (*gpioMap)[gpioNr] = config_gpio_pin;

            if (pinMode == GPIO_PIN_MODE_EXTERNAL_FLASH_WS281X)
            {
                ESP_LOGD(TAG, "Set WS2812 to GPIO %d", gpioNr);
                gpioExtLED = gpioNr;
            }

            if (intType != GPIO_INTR_DISABLE)
            {
                registerISR = true;
            }
        }

        if (toUpper(splitted[0]) == "LEDNUMBERS")
        {
            LEDNumbers = stoi(splitted[1]);
        }

        if (toUpper(splitted[0]) == "LEDCOLOR")
        {
            uint8_t _r, _g, _b;
            _r = stoi(splitted[1]);
            _g = stoi(splitted[2]);
            _b = stoi(splitted[3]);

            LEDColor = Rgb{_r, _g, _b};
        }

        if (toUpper(splitted[0]) == "LEDTYPE")
        {
            if (splitted[1] == "WS2812")
            {
                LEDType = LED_WS2812;
            }
            else if (splitted[1] == "WS2812B")
            {
                LEDType = LED_WS2812B;
            }
            else if (splitted[1] == "SK6812")
            {
                LEDType = LED_SK6812;
            }
            else if (splitted[1] == "WS2813")
            {
                LEDType = LED_WS2813;
            }
        }
    }

    if (registerISR)
    {
        // install gpio isr service
        gpio_install_isr_service(ESP_INTR_FLAG_LOWMED | ESP_INTR_FLAG_IRAM);
    }

    if (gpioExtLED > 0)
    {
    }

    return true;
}

void ConfigGpioHandler::clear()
{
    ESP_LOGD(TAG, "GpioHandler::clear");

    if (gpioMap != NULL)
    {
        for (std::map<gpio_num_t, ConfigGpioPin *>::iterator it = gpioMap->begin(); it != gpioMap->end(); ++it)
        {
            delete it->second;
        }
        gpioMap->clear();
    }
}

void ConfigGpioHandler::registerGpioUri()
{
    ESP_LOGI(TAG, "Registering URI handlers");

    httpd_uri_t camuri = {};
    camuri.method = HTTP_GET;
    camuri.uri = "/GPIO";
    camuri.handler = APPLY_BASIC_AUTH_FILTER(callHandleHttpRequest);
    camuri.user_ctx = (void *)this;
    httpd_register_uri_handler(_httpServer, &camuri);
}

esp_err_t ConfigGpioHandler::handleHttpRequest(httpd_req_t *req)
{
    ESP_LOGD(TAG, "handleHttpRequest");

    if (gpioMap == NULL)
    {
        std::string resp_str = "GPIO handler not initialized";
        httpd_resp_send(req, resp_str.c_str(), resp_str.length());
        return ESP_OK;
    }

#ifdef DEBUG_DETAIL_ON
    LogFile.WriteHeapInfo("handler_switch_GPIO - Start");
#endif

    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "handler_switch_GPIO");
    char _query[200];
    char _valueGPIO[30];
    char _valueStatus[30];
    std::string gpio, status;

    if (httpd_req_get_url_query_str(req, _query, 200) == ESP_OK)
    {
        ESP_LOGD(TAG, "Query: %s", _query);

        if (httpd_query_key_value(_query, "GPIO", _valueGPIO, 30) == ESP_OK)
        {
            ESP_LOGD(TAG, "GPIO is found %s", _valueGPIO);
            gpio = std::string(_valueGPIO);
        }
        else
        {
            std::string resp_str = "GPIO No is not defined";
            httpd_resp_send(req, resp_str.c_str(), resp_str.length());
            return ESP_OK;
        }

        if (httpd_query_key_value(_query, "Status", _valueStatus, 30) == ESP_OK)
        {
            ESP_LOGD(TAG, "Status is found %s", _valueStatus);
            status = std::string(_valueStatus);
        }
    }
    else
    {
        const char *resp_str = "Error in call. Use /GPIO?GPIO=12&Status=high";
        httpd_resp_send(req, resp_str, strlen(resp_str));
        return ESP_OK;
    }

    status = toUpper(status);
    if ((status != "HIGH") && (status != "LOW") && (status != "TRUE") && (status != "FALSE") && (status != "0") && (status != "1") && (status != ""))
    {
        std::string zw = "Status not valid: " + status;
        httpd_resp_sendstr_chunk(req, zw.c_str());
        httpd_resp_sendstr_chunk(req, NULL);
        return ESP_OK;
    }

    int gpionum = stoi(gpio);

    // frei: 16; 12-15; 2; 4  // nur 12 und 13 funktionieren 2: reboot, 4: BlitzLED, 15: PSRAM, 14/15: DMA für SDKarte ???
    gpio_num_t gpio_num = resolvePinNr(gpionum);
    if (gpio_num == GPIO_NUM_NC)
    {
        std::string zw = "GPIO" + std::to_string(gpionum) + " unsupported - only 12 & 13 free";
        httpd_resp_sendstr_chunk(req, zw.c_str());
        httpd_resp_sendstr_chunk(req, NULL);
        return ESP_OK;
    }

    if (gpioMap->count(gpio_num) == 0)
    {
        char resp_str[30];
        sprintf(resp_str, "GPIO%d is not registred", gpio_num);
        httpd_resp_send(req, resp_str, strlen(resp_str));
        return ESP_OK;
    }

    if (status == "")
    {
        std::string resp_str = "";
        status = (*gpioMap)[gpio_num]->getValue(&resp_str) ? "HIGH" : "LOW";
        if (resp_str == "")
        {
            resp_str = status;
        }
        httpd_resp_sendstr_chunk(req, resp_str.c_str());
        httpd_resp_sendstr_chunk(req, NULL);
    }
    else
    {
        std::string resp_str = "";
        (*gpioMap)[gpio_num]->setValue((status == "HIGH") || (status == "TRUE") || (status == "1"), GPIO_SET_SOURCE_HTTP, &resp_str);
        if (resp_str == "")
        {
            resp_str = "GPIO" + std::to_string(gpionum) + " switched to " + status;
        }
        httpd_resp_sendstr_chunk(req, resp_str.c_str());
        httpd_resp_sendstr_chunk(req, NULL);
    }

    return ESP_OK;
};

void ConfigGpioHandler::flashLightEnable(bool value)
{
    ESP_LOGD(TAG, "GpioHandler::flashLightEnable %s", value ? "true" : "false");

    if (gpioMap != NULL)
    {
        for (std::map<gpio_num_t, ConfigGpioPin *>::iterator it = gpioMap->begin(); it != gpioMap->end(); ++it)
        {
            if (it->second->getMode() == GPIO_PIN_MODE_BUILT_IN_FLASH_LED) //|| (it->second->getMode() == GPIO_PIN_MODE_EXTERNAL_FLASH_PWM) || (it->second->getMode() == GPIO_PIN_MODE_EXTERNAL_FLASH_WS281X))
            {
                std::string resp_str = "";
                it->second->setValue(value, GPIO_SET_SOURCE_INTERNAL, &resp_str);

                if (resp_str == "")
                {
                    ESP_LOGD(TAG, "Flash light pin GPIO %d switched to %s", (int)it->first, (value ? "on" : "off"));
                }
                else
                {
                    ESP_LOGE(TAG, "Can't set flash light pin GPIO %d.  Error: %s", (int)it->first, resp_str.c_str());
                }
            }
            else
            {
                if (it->second->getMode() == GPIO_PIN_MODE_EXTERNAL_FLASH_WS281X)
                {
#ifdef __LEDGLOBAL
                    if (leds_global == NULL)
                    {
                        ESP_LOGI(TAG, "init SmartLed: LEDNumber=%d, GPIO=%d", LEDNumbers, (int)it->second->getGPIO());
                        leds_global = new SmartLed(LEDType, LEDNumbers, it->second->getGPIO(), 0, DoubleBuffer);
                    }
                    else
                    {
                        // wait until we can update: https://github.com/RoboticsBrno/SmartLeds/issues/10#issuecomment-386921623
                        leds_global->wait();
                    }
#else
                    SmartLed leds(LEDType, LEDNumbers, it->second->getGPIO(), 0, DoubleBuffer);
#endif

                    if (value)
                    {
                        for (int i = 0; i < LEDNumbers; ++i)
                        {
#ifdef __LEDGLOBAL
                            (*leds_global)[i] = LEDColor;
#else
                            leds[i] = LEDColor;
#endif
                        }
                    }
                    else
                    {
                        for (int i = 0; i < LEDNumbers; ++i)
                        {
#ifdef __LEDGLOBAL
                            (*leds_global)[i] = Rgb{0, 0, 0};
#else
                            leds[i] = Rgb{0, 0, 0};
#endif
                        }
                    }
#ifdef __LEDGLOBAL
                    leds_global->show();
#else
                    leds.show();
#endif
                }
            }
        }
    }
}

gpio_num_t ConfigGpioHandler::resolvePinNr(uint8_t pinNr)
{
    switch (pinNr)
    {
    case 0:
        return GPIO_NUM_0;
    case 1:
        return GPIO_NUM_1;
    case 3:
        return GPIO_NUM_3;
    case 4:
        return GPIO_NUM_4;
    case 12:
        return GPIO_NUM_12;
    case 13:
        return GPIO_NUM_13;
    default:
        return GPIO_NUM_NC;
    }
}

config_gpio_pin_mode_t ConfigGpioHandler::resolvePinMode(std::string input)
{
    config_gpio_pin_mode_t config_gpio_pin_mode = GPIO_PIN_MODE_DISABLED;

    if (input == "input")
    {
        config_gpio_pin_mode = GPIO_PIN_MODE_INPUT;
    }
    else if (input == "input-pullup")
    {
        config_gpio_pin_mode = GPIO_PIN_MODE_INPUT_PULLUP;
    }
    else if (input == "input-pulldown")
    {
        config_gpio_pin_mode = GPIO_PIN_MODE_INPUT_PULLDOWN;
    }
    else if (input == "output")
    {
        config_gpio_pin_mode = GPIO_PIN_MODE_OUTPUT;
    }
    else if (input == "built-in-led")
    {
        config_gpio_pin_mode = GPIO_PIN_MODE_BUILT_IN_FLASH_LED;
    }
    else if (input == "output-pwm")
    {
        config_gpio_pin_mode = GPIO_PIN_MODE_OUTPUT_PWM;
    }
    else if (input == "external-flash-pwm")
    {
        config_gpio_pin_mode = GPIO_PIN_MODE_EXTERNAL_FLASH_PWM;
    }
    else if (input == "external-flash-ws281x")
    {
        config_gpio_pin_mode = GPIO_PIN_MODE_EXTERNAL_FLASH_WS281X;
    }

    return config_gpio_pin_mode;
}

gpio_int_type_t ConfigGpioHandler::resolveIntType(std::string input)
{
    gpio_int_type_t gpio_int_type = GPIO_INTR_DISABLE;

    if (input == "rising-edge")
    {
        gpio_int_type = GPIO_INTR_POSEDGE;
    }
    else if (input == "falling-edge")
    {
        gpio_int_type = GPIO_INTR_NEGEDGE;
    }
    else if (input == "rising-and-falling")
    {
        gpio_int_type = GPIO_INTR_ANYEDGE;
    }
    else if (input == "low-level-trigger")
    {
        gpio_int_type = GPIO_INTR_LOW_LEVEL;
    }
    else if (input == "high-level-trigger")
    {
        gpio_int_type = GPIO_INTR_HIGH_LEVEL;
    }

    return gpio_int_type;
}

ConfigGpioHandler *gpio_handler_get()
{
    return gpioHandler;
}

esp_err_t callHandleHttpRequest(httpd_req_t *req)
{
    ESP_LOGD(TAG, "callHandleHttpRequest");
    ConfigGpioHandler *gpioHandler = (ConfigGpioHandler *)req->user_ctx;
    return gpioHandler->handleHttpRequest(req);
}

void taskGpioHandler(void *pvParameter)
{
    ESP_LOGD(TAG, "taskGpioHandler");
    ((ConfigGpioHandler *)pvParameter)->init();
}

void gpio_handler_create(httpd_handle_t server)
{
    if (gpioHandler == NULL)
    {
        gpioHandler = new ConfigGpioHandler(CONFIG_FILE, server);
    }
}

void gpio_handler_init()
{
    if (gpioHandler != NULL)
    {
        gpioHandler->init();
    }
}

void gpio_handler_deinit()
{
    if (gpioHandler != NULL)
    {
        gpioHandler->deinit();
    }
}

void gpio_handler_destroy()
{
    if (gpioHandler != NULL)
    {
        gpio_handler_deinit();
        delete gpioHandler;
        gpioHandler = NULL;
    }
}
