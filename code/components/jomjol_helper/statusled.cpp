#include "statusled.h"

#include <sys/types.h>
#include <sys/stat.h>
#include "driver/gpio.h"

#include "ClassLogFile.h"
#include "defines.h"

// define `gpio_pad_select_gpip` for newer versions of IDF
#if (ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 3, 0))
#include "esp_rom_gpio.h"
#define gpio_pad_select_gpio esp_rom_gpio_pad_select_gpio
#endif

static const char *TAG = "STATUSLED";

TaskHandle_t xHandle_task_status_led = NULL;
struct status_led_data status_led_data = {};

void task_status_led(void *pvParameter)
{
	// ESP_LOGD(TAG, "task_status_led - create");
	while (status_led_data.bProcessingRequest)
	{
		// ESP_LOGD(TAG, "task_status_led - start");
		struct status_led_data status_led_data_int = status_led_data;

		gpio_pad_select_gpio(BLINK_GPIO);				  // Init the GPIO
		gpio_set_direction(BLINK_GPIO, GPIO_MODE_OUTPUT); // Set the GPIO as a push/pull output

		// LED off
#ifdef BLINK_GPIO_INVERT
		gpio_set_level(BLINK_GPIO, 1);
#else
		gpio_set_level(BLINK_GPIO, 0);
#endif

		// Default: repeat 2 times
		for (int i = 0; i < 2;)
		{
			if (!status_led_data_int.bInfinite)
			{
				++i;
			}

			for (int j = 0; j < status_led_data_int.iSourceBlinkCnt; ++j)
			{
#ifdef BLINK_GPIO_INVERT
				gpio_set_level(BLINK_GPIO, 0);
#else
				gpio_set_level(BLINK_GPIO, 1);
#endif
				vTaskDelay(status_led_data_int.iBlinkTime / portTICK_PERIOD_MS);
#ifdef BLINK_GPIO_INVERT
				gpio_set_level(BLINK_GPIO, 1);
#else
				gpio_set_level(BLINK_GPIO, 0);
#endif
				vTaskDelay(status_led_data_int.iBlinkTime / portTICK_PERIOD_MS);
			}

			vTaskDelay(500 / portTICK_PERIOD_MS); // Delay between module code and error code

			for (int j = 0; j < status_led_data_int.iCodeBlinkCnt; ++j)
			{
#ifdef BLINK_GPIO_INVERT
				gpio_set_level(BLINK_GPIO, 0);
#else
				gpio_set_level(BLINK_GPIO, 1);
#endif
				vTaskDelay(status_led_data_int.iBlinkTime / portTICK_PERIOD_MS);
#ifdef BLINK_GPIO_INVERT
				gpio_set_level(BLINK_GPIO, 1);
#else
				gpio_set_level(BLINK_GPIO, 0);
#endif
				vTaskDelay(status_led_data_int.iBlinkTime / portTICK_PERIOD_MS);
			}
			vTaskDelay(1500 / portTICK_PERIOD_MS); // Delay to signal new round
		}

		status_led_data.bProcessingRequest = false;
		// ESP_LOGD(TAG, "task_status_led - done/wait");
		vTaskDelay(10000 / portTICK_PERIOD_MS); // Wait for an upcoming request otherwise continue and delete task to save memory
	}

	// ESP_LOGD(TAG, "task_status_led - delete");
	xHandle_task_status_led = NULL;
	vTaskDelete(NULL); // Delete this task due to no request
}

void setStatusLED(status_led_source _eSource, int _iCode, bool _bInfinite)
{
	if (BLINK_GPIO != GPIO_NUM_NC)
	{
		// ESP_LOGD(TAG, "StatusLED - start");

		if (_eSource == WLAN_CONN)
		{
			status_led_data.iSourceBlinkCnt = WLAN_CONN;
			status_led_data.iCodeBlinkCnt = _iCode;
			status_led_data.iBlinkTime = 250;
			status_led_data.bInfinite = _bInfinite;
		}
		else if (_eSource == WLAN_INIT)
		{
			status_led_data.iSourceBlinkCnt = WLAN_INIT;
			status_led_data.iCodeBlinkCnt = _iCode;
			status_led_data.iBlinkTime = 250;
			status_led_data.bInfinite = _bInfinite;
		}
		else if (_eSource == SDCARD_INIT)
		{
			status_led_data.iSourceBlinkCnt = SDCARD_INIT;
			status_led_data.iCodeBlinkCnt = _iCode;
			status_led_data.iBlinkTime = 250;
			status_led_data.bInfinite = _bInfinite;
		}
		else if (_eSource == SDCARD_CHECK)
		{
			status_led_data.iSourceBlinkCnt = SDCARD_CHECK;
			status_led_data.iCodeBlinkCnt = _iCode;
			status_led_data.iBlinkTime = 250;
			status_led_data.bInfinite = _bInfinite;
		}
		else if (_eSource == CAM_INIT)
		{
			status_led_data.iSourceBlinkCnt = CAM_INIT;
			status_led_data.iCodeBlinkCnt = _iCode;
			status_led_data.iBlinkTime = 250;
			status_led_data.bInfinite = _bInfinite;
		}
		else if (_eSource == PSRAM_INIT)
		{
			status_led_data.iSourceBlinkCnt = PSRAM_INIT;
			status_led_data.iCodeBlinkCnt = _iCode;
			status_led_data.iBlinkTime = 250;
			status_led_data.bInfinite = _bInfinite;
		}
		else if (_eSource == TIME_CHECK)
		{
			status_led_data.iSourceBlinkCnt = TIME_CHECK;
			status_led_data.iCodeBlinkCnt = _iCode;
			status_led_data.iBlinkTime = 250;
			status_led_data.bInfinite = _bInfinite;
		}
		else if (_eSource == AP_OR_OTA)
		{
			status_led_data.iSourceBlinkCnt = AP_OR_OTA;
			status_led_data.iCodeBlinkCnt = _iCode;
			status_led_data.iBlinkTime = 350;
			status_led_data.bInfinite = _bInfinite;
		}

		if (xHandle_task_status_led && !status_led_data.bProcessingRequest)
		{
			status_led_data.bProcessingRequest = true;
			BaseType_t xReturned = xTaskAbortDelay(xHandle_task_status_led); // Reuse still running status LED task
		}
		else if (xHandle_task_status_led == NULL)
		{
			status_led_data.bProcessingRequest = true;
			BaseType_t xReturned = xTaskCreate(&task_status_led, "task_status_led", 1280, NULL, tskIDLE_PRIORITY + 1, &xHandle_task_status_led);
			if (xReturned != pdPASS)
			{
				xHandle_task_status_led = NULL;
				LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "task_status_led failed to create");
				LogFile.WriteHeapInfo("task_status_led failed");
				// ESP_LOGE(TAG, "task_status_led failed to create");
			}
		}
		else
		{
			ESP_LOGD(TAG, "task_status_led still processing, request skipped"); // Requests with high frequency could be skipped, but LED is only helpful for static states
		}
		// ESP_LOGD(TAG, "StatusLED - done");
	}
}

void setStatusLEDOff(void)
{
	if (BLINK_GPIO != GPIO_NUM_NC)
	{
		if (xHandle_task_status_led)
		{
			vTaskDelete(xHandle_task_status_led); // Delete task for StatusLED to force stop of blinking
		}

		gpio_pad_select_gpio(BLINK_GPIO);				  // Init the GPIO
		gpio_set_direction(BLINK_GPIO, GPIO_MODE_OUTPUT); // Set the GPIO as a push/pull output

		// LED off
#ifdef BLINK_GPIO_INVERT
		gpio_set_level(BLINK_GPIO, 1);
#else
		gpio_set_level(BLINK_GPIO, 0);
#endif
	}
}
