#include <iostream>
#include <string>
#include <vector>
#include <regex>
#include <esp_psram.h>
#include <esp_pm.h>
#include <psram.h>

#include <esp_chip_info.h>
#include <driver/rtc_io.h>

#include <esp_vfs_fat.h>
#include <ffconf.h>
#include <driver/sdmmc_host.h>

#ifdef DISABLE_BROWNOUT_DETECTOR
#include <soc/soc.h>
#include <soc/rtc_cntl_reg.h>
#endif

// define `gpio_pad_select_gpip` for newer versions of IDF
#if (ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 3, 0))
#include <esp_rom_gpio.h>
#define gpio_pad_select_gpio esp_rom_gpio_pad_select_gpio
#endif

#include "defines.h"
#include "Helper.h"

#include "MainFlowControl.h"
#include "ClassLogFile.h"

#include "connect_wifi_ap.h"
#include "connect_wifi_sta.h"
#include "read_network_config.h"

#include "server_main.h"
#include "server_file.h"
#include "server_ota.h"
#include "server_camera.h"
#include "server_GpioHandler.h"

#ifdef ENABLE_MQTT
#include "server_mqtt.h"
#endif // ENABLE_MQTT

#include "time_sntp.h"
#include "configFile.h"
#include "statusled.h"
#include "sdcard_check.h"
#include "basic_auth.h"

extern const char *GIT_TAG;
extern const char *GIT_REV;
extern const char *GIT_BRANCH;
extern const char *BUILD_TIME;

extern std::string getFwVersion(void);
extern std::string getHTMLversion(void);
extern std::string getHTMLcommit(void);

static const char *TAG = "MAIN";

esp_err_t Init_NVS(void)
{
    ESP_LOGI(TAG, "Initialize NVS");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        // NVS partition was truncated and needs to be erased, retry nvs_flash_init
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }

    return ret;
}

esp_err_t Init_SDCard(void)
{
    ESP_LOGI(TAG, "Initialize SD Card");

    esp_err_t ret = ESP_OK;

    ESP_LOGD(TAG, "Using SDMMC peripheral");
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;

    // For SoCs where the SD power can be supplied both via an internal or external (e.g. on-board LDO) power supply.
    // When using specific IO pins (which can be used for ultra high-speed SDMMC) to connect to the SD card
    // and the internal LDO power supply, we need to initialize the power supply first.
#if SD_PWR_CTRL_LDO_INTERNAL_IO
    sd_pwr_ctrl_ldo_config_t ldo_config = {
        .ldo_chan_id = CONFIG_EXAMPLE_SD_PWR_CTRL_LDO_IO_ID,
    };
    sd_pwr_ctrl_handle_t pwr_ctrl_handle = NULL;

    ret = sd_pwr_ctrl_new_on_chip_ldo(&ldo_config, &pwr_ctrl_handle);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to create a new on-chip LDO power control driver");
        return ret;
    }
    host.pwr_ctrl_handle = pwr_ctrl_handle;
#endif

    // This initializes the slot without card detect (CD) and write protect (WP) signals.
    // Modify slot_config.gpio_cd and slot_config.gpio_wp if your board has these signals.
#ifdef CONFIG_SOC_SDMMC_USE_GPIO_MATRIX
    sdmmc_slot_config_t slot_config = {
        .cd = SDMMC_SLOT_NO_CD,
        .wp = SDMMC_SLOT_NO_WP,
    };
#else
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
#endif

    // Set bus width to use:
#ifdef __SD_USE_ONE_LINE_MODE__
    slot_config.width = 1;
#ifdef CONFIG_SOC_SDMMC_USE_GPIO_MATRIX
    slot_config.clk = GPIO_SDCARD_CLK;
    slot_config.cmd = GPIO_SDCARD_CMD;
    slot_config.d0 = GPIO_SDCARD_D0;
#endif // end CONFIG_SOC_SDMMC_USE_GPIO_MATRIX
#else  // else __SD_USE_ONE_LINE_MODE__
    slot_config.width = 4;
#ifdef CONFIG_SOC_SDMMC_USE_GPIO_MATRIX
    slot_config.d1 = GPIO_SDCARD_D1;
    slot_config.d2 = GPIO_SDCARD_D2;
    slot_config.d3 = GPIO_SDCARD_D3;
#endif // end CONFIG_SOC_SDMMC_USE_GPIO_MATRIX
#endif // end __SD_USE_ONE_LINE_MODE__

    // Enable internal pullups on enabled pins. The internal pullups
    // are insufficient however, please make sure 10k external pullups are
    // connected on the bus. This is for debug / example purpose only.
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    // Der PullUp des GPIO13 wird durch slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
    // nicht gesetzt, da er eigentlich nicht benötigt wird,
    // dies führt jedoch bei schlechten Kopien des AI_THINKER Boards
    // zu Problemen mit der SD Initialisierung und eventuell sogar zur reboot-loops.
    // Um diese Probleme zu kompensieren, wird der PullUp manuel gesetzt.
    gpio_set_pull_mode(GPIO_SDCARD_D3, GPIO_PULLUP_ONLY); // HS2_D3

    // Options for mounting the filesystem.
    // If format_if_mount_failed is set to true, SD card will be partitioned and
    // formatted in case when mounting fails.
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 12,           // previously -> 2022-09-21: 5, 2023-01-02: 7
        .allocation_unit_size = 0, // 0 = auto
        .disk_status_check_enable = 0,
    };

    sdmmc_card_t *card;

    // Use settings defined above to initialize SD card and mount FAT filesystem.
    // Note: esp_vfs_fat_sdmmc_mount is an all-in-one convenience function.
    // Please check its source code and implement error recovery when developing
    // production applications.
    ret = esp_vfs_fat_sdmmc_mount(SD_PARTITION_PATH, &host, &slot_config, &mount_config, &card);

    if (ret != ESP_OK)
    {
        if (ret == ESP_FAIL)
        {
            setStatusLED(SDCARD_INIT, 1, true);
            ESP_LOGE(TAG, "Failed to mount FAT filesystem on SD card. Check SD card filesystem (only FAT supported) or try another card");
        }
        else if (ret == ESP_ERR_TIMEOUT)
        {
            // Error code: 0x107 --> usually: SD not found
            setStatusLED(SDCARD_INIT, 2, true);
            ESP_LOGE(TAG, "SD card init failed. Check if SD card is properly inserted into SD card slot or try another card");
        }
        else
        {
            setStatusLED(SDCARD_INIT, 3, true);
            ESP_LOGE(TAG, "SD card init failed. Check error code or try another card");
        }
        return ret;
    }

    SaveSDCardInfo(card);
    return ret;
}

esp_err_t Check_SDCardRW(void)
{
    ESP_LOGI(TAG, "SD card: basic R/W check");

    // SD card: basic R/W check
    // ********************************************
    int iSDCardStatus = SDCardCheckRW();
    if (iSDCardStatus < 0)
    {
        if (iSDCardStatus <= -1 && iSDCardStatus >= -2)
        {
            // write error
            setStatusLED(SDCARD_CHECK, 1, true);
            ESP_LOGE(TAG, "SD card: basic R/W check: write error!");
        }
        else if (iSDCardStatus <= -3 && iSDCardStatus >= -5)
        {
            // read error
            setStatusLED(SDCARD_CHECK, 2, true);
            ESP_LOGE(TAG, "SD card: basic R/W check: read error!");
        }
        else if (iSDCardStatus == -6)
        {
            // delete error
            setStatusLED(SDCARD_CHECK, 3, true);
            ESP_LOGE(TAG, "SD card: basic R/W check: delete error!");
        }

        return ESP_FAIL;
    }
    else
    {
        // SD card: Create further mandatory directories (if not already existing)
        // Correct creation of these folders will be checked with function "SDCardCheckFolderFilePresence"
        // ********************************************
        bool ret = true;
        ret &= MakeDir("/sdcard/firmware");     // mandatory for OTA firmware update
        ret &= MakeDir("/sdcard/img_tmp");      // mandatory for setting up alignment marks
        ret &= MakeDir("/sdcard/demo");         // mandatory for demo mode
        ret &= MakeDir("/sdcard/config/certs"); // mandatory for mqtt certificates

        // SD card: Create log directories (if not already existing)
        // ********************************************
        ret &= LogFile.CreateLogDirectories(); // mandatory for logging + image saving

        if (!ret)
        {
            // write error
            setStatusLED(SDCARD_CHECK, 1, true);
            return ESP_FAIL;
        }
    }

    return ESP_OK;
}

esp_err_t Init_PSRAM(void)
{
    ESP_LOGI(TAG, "Initialize PSRAM");

    // Init external PSRAM
    // ********************************************
    esp_err_t ret = ESP_OK;
    ret = esp_psram_init();

    if (ret == ESP_FAIL)
    {
        // ESP_FAIL -> Failed to init PSRAM
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "PSRAM init failed (" + std::string(esp_err_to_name(ret)) + ")! PSRAM not found or defective");
        setSystemStatusFlag(SYSTEM_STATUS_PSRAM_BAD);
        setStatusLED(PSRAM_INIT, 1, true);
    }
    else
    {
        // ESP_OK -> PSRAM init OK --> continue to check PSRAM size
        size_t psram_size = esp_psram_get_size(); // size_t psram_size = esp_psram_get_size(); // comming in IDF 5.0
        LogFile.WriteToFile(ESP_LOG_INFO, TAG, "PSRAM size: " + std::to_string(psram_size) + " byte (" + std::to_string(psram_size / 1024 / 1024) + "MB / " + std::to_string(psram_size / 1024 / 1024 * 8) + "MBit)");

        // Check PSRAM size
        // ********************************************
        if (psram_size < (4 * 1024 * 1024))
        {
            // PSRAM is below 4 MBytes (32Mbit)
            LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "PSRAM size >= 4MB (32Mbit) is mandatory to run this application");
            setSystemStatusFlag(SYSTEM_STATUS_PSRAM_BAD);
            setStatusLED(PSRAM_INIT, 2, true);
            ret = ESP_FAIL;
        }
        else
        {
            // PSRAM size OK --> continue to check heap size
            size_t _hsize = getESPHeapSize();
            LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Total heap: " + std::to_string(_hsize) + " byte");

            // Check heap memory
            // ********************************************
            if (_hsize < 4000000)
            {
                // Check available Heap memory for a bit less than 4 MB (a test on a good device showed 4187558 bytes to be available)
                LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Total heap >= 4000000 byte is mandatory to run this application");
                setSystemStatusFlag(SYSTEM_STATUS_HEAP_TOO_SMALL);
                setStatusLED(PSRAM_INIT, 3, true);
                ret = ESP_FAIL;
            }
            else
            {
                // HEAP size OK --> continue to reserve shared memory block and check camera init
                /* Allocate static PSRAM memory regions */
                if (reserve_psram_shared_region() == false)
                {
                    LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Allocate static PSRAM memory regions failed!");
                    setSystemStatusFlag(SYSTEM_STATUS_HEAP_TOO_SMALL);
                    setStatusLED(PSRAM_INIT, 3, true);
                    ret = ESP_FAIL;
                }
                else
                {
                    LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Allocate static PSRAM memory regions ok.");
                    ret = ESP_OK;
                }
            }
        }
    }

    return ret;
}

esp_err_t Init_Camera(void)
{
    ESP_LOGI(TAG, "Initialize Camera");

    // Init Camera
    // ********************************************
    Camera.PowerResetCamera();

    esp_err_t ret = Camera.InitCam();
    Camera.FlashLightOnOff(false, Camera.LedIntensity);

    TickType_t xDelay = 2000 / portTICK_PERIOD_MS;
    ESP_LOGD(TAG, "After camera initialization: sleep for: %ldms", (long)xDelay * CONFIG_FREERTOS_HZ / portTICK_PERIOD_MS);
    vTaskDelay(xDelay);

    // Check camera init
    // ********************************************
    if (ret != ESP_OK)
    {
        // Camera init failed, retry to init
        char camStatusHex[33];
        sprintf(camStatusHex, "0x%02x", ret);
        LogFile.WriteToFile(ESP_LOG_WARN, TAG, "Camera init failed (" + std::string(camStatusHex) + "), retrying...");

        Camera.PowerResetCamera();
        ret = Camera.InitCam();
        Camera.FlashLightOnOff(false, Camera.LedIntensity);

        xDelay = 2000 / portTICK_PERIOD_MS;
        ESP_LOGD(TAG, "After camera initialization: sleep for: %ldms", (long)xDelay * CONFIG_FREERTOS_HZ / portTICK_PERIOD_MS);
        vTaskDelay(xDelay);

        if (ret != ESP_OK)
        {
            // Camera init failed again
            sprintf(camStatusHex, "0x%02x", ret);
            LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Camera init failed (" + std::string(camStatusHex) + ")! Check camera module and/or proper electrical connection");
            setSystemStatusFlag(SYSTEM_STATUS_CAM_BAD);
            Camera.FlashLightOnOff(false, Camera.LedIntensity); // make sure flashlight is off
            setStatusLED(CAM_INIT, 1, true);
        }
    }

    if (ret == ESP_OK)
    {
        // ESP_OK -> Camera init OK --> continue to perform camera framebuffer check
        // Camera framebuffer check
        // ********************************************
        if (!Camera.testCamera())
        {
            LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Camera framebuffer check failed");
            // ESP_LOGE(TAG, "Camera framebuffer check failed");
            // Easiest would be to simply restart here and try again,
            // how ever there seem to be systems where it fails at startup but still work correctly later.
            // Therefore we treat it still as successed! */
            setSystemStatusFlag(SYSTEM_STATUS_CAM_FB_BAD);
            setStatusLED(CAM_INIT, 2, false);
        }
        Camera.FlashLightOnOff(false, Camera.LedIntensity); // make sure flashlight is off before start of flow

        // Print camera infos
        // ********************************************
        char caminfo[50];
        sensor_t *s = esp_camera_sensor_get();
        sprintf(caminfo, "PID: 0x%02x, VER: 0x%02x, MIDL: 0x%02x, MIDH: 0x%02x", s->id.PID, s->id.VER, s->id.MIDH, s->id.MIDL);
        LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Camera info: " + std::string(caminfo));
    }

    return ret;
}

extern "C" void app_main(void)
{
    TickType_t xDelay;

#ifdef DISABLE_BROWNOUT_DETECTOR
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); // disable brownout detector
#endif

#if (defined(BOARD_ESP32_S3_ETH_V1) || defined(BOARD_ESP32_S3_ETH_V2))
    // Configure IO Pad as General Purpose IO,
    // so that it can be connected to internal Matrix,
    // then combined with one or more peripheral signals.
    gpio_pad_select_gpio(PER_ENABLE);

    gpio_set_direction(PER_ENABLE, GPIO_MODE_OUTPUT);
    gpio_set_level(PER_ENABLE, 1);
#endif

    rtc_gpio_hold_dis(GPIO_NUM_12);
    rtc_gpio_hold_dis(GPIO_NUM_4);

    // ********************************************
    // Highlight start of app_main
    // ********************************************
    ESP_LOGI(TAG, "\n\n\n\n================ Start app_main =================");

    // Init NVS
    // ********************************************
    if (Init_NVS() != ESP_OK)
    {
        ESP_LOGE(TAG, "Device init aborted in step Init_NVS()!");
        return;
    }

    // Init SD card
    // ********************************************
    if (Init_SDCard() != ESP_OK)
    {
        ESP_LOGE(TAG, "Device init aborted in step Init_SDCard()!");
        return; // No way to continue without working SD card!
    }

    if (Check_SDCardRW() != ESP_OK)
    {
        ESP_LOGE(TAG, "Device init aborted in step Check_SDCardRW()!");
        return; // No way to continue without working SD card!
    }

    // SD card: Check presence of some mandatory folders / files
    // ********************************************
    if (!SDCardCheckFolderFilePresence())
    {
        setStatusLED(SDCARD_CHECK, 4, true);
        setSystemStatusFlag(SYSTEM_STATUS_FOLDER_CHECK_BAD); // reduced web interface going to be loaded
    }

    // Init external PSRAM
    // ********************************************
    if (Init_PSRAM() != ESP_OK)
    {
        ESP_LOGE(TAG, "Device init aborted in step Init_PSRAM()!");
        return; // No way to continue without working SD card!
    }

    // Check for updates
    // ********************************************
    checkOTAUpdate();
    checkUpdate();

#if defined(CONFIG_SOC_TEMP_SENSOR_SUPPORTED)
    // temperature sensor
    // ********************************************
    initTempsensor();
#endif

    // Init external PSRAM
    // ********************************************
    if (Init_Camera() != ESP_OK)
    {
        ESP_LOGE(TAG, "Device init aborted in step Init_Camera()!");
        return; // No way to continue without working SD card!
    }

    // Init time (as early as possible, but SD card needs to be initialized)
    // ********************************************
    setupTime(); // NTP time service: Status of time synchronization will be checked after every round (server_tflite.cpp)

    // Check version information
    // ********************************************
    std::string versionFormated = getFwVersion() + ", Date/Time: " + std::string(BUILD_TIME) + ", Web UI: " + getHTMLversion();
    if (std::string(GIT_TAG) != "")
    {
        // We are on a tag, add it as prefix
        versionFormated = "Tag: '" + std::string(GIT_TAG) + "', " + versionFormated;
    }
    LogFile.WriteToFile(ESP_LOG_INFO, TAG, versionFormated);

    if (getHTMLcommit().substr(0, 7) == "?")
    {
        LogFile.WriteToFile(ESP_LOG_WARN, TAG, std::string("Failed to read file html/version.txt to parse Web UI version"));
    }

    if (getHTMLcommit().substr(0, 7) != std::string(GIT_REV).substr(0, 7))
    {
        // Compare the first 7 characters of both hashes
        LogFile.WriteToFile(ESP_LOG_WARN, TAG, "Web UI version (" + getHTMLcommit() + ") does not match firmware version (" + std::string(GIT_REV) + ")");
        LogFile.WriteToFile(ESP_LOG_WARN, TAG, "Recommendation: Repeat installation using AI-on-the-edge-device__update__*.zip");
    }

    // Check reboot reason
    // ********************************************
    CheckIsPlannedReboot();
    if (!getIsPlannedReboot() && (esp_reset_reason() == ESP_RST_PANIC) && (esp_reset_reason() != ESP_RST_DEEPSLEEP))
    {
        // If system reboot was not triggered by user and reboot was caused by execption
        LogFile.WriteToFile(ESP_LOG_WARN, TAG, "Reset reason: " + getResetReason());
        LogFile.WriteToFile(ESP_LOG_WARN, TAG, "Device was rebooted due to a software exception! Log level is set to DEBUG until the next reboot. "
                                               "Flow init is delayed by 5 minutes to check the logs or do an OTA update");
        LogFile.WriteToFile(ESP_LOG_WARN, TAG, "Keep device running until crash occurs again and check logs after device is up again");
        LogFile.setLogLevel(ESP_LOG_DEBUG);
    }
    else if (esp_reset_reason() == ESP_RST_DEEPSLEEP)
    {
        LogFile.WriteToFile(ESP_LOG_INFO, TAG, "System Reboot was triggered by deep sleep");
    }
    else
    {
        LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Reset reason: " + getResetReason());
    }

    // start Network
    // ********************************************
    if (Init_Network() != ESP_OK)
    {
        ESP_LOGE(TAG, "!!! Device init aborted at step: Init_Network() !!!");
        return; // No way to continue without working Network!
    }

    xDelay = 2000 / portTICK_PERIOD_MS;
    ESP_LOGD(TAG, "main: sleep for: %ldms", (long)xDelay * CONFIG_FREERTOS_HZ / portTICK_PERIOD_MS);
    vTaskDelay(xDelay);

    // manual reset the time
    // ********************************************
    if (!time_manual_reset_sync())
    {
        LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "Manual Time Sync failed during startup");
    }

    // Print Device info
    // ********************************************
    esp_chip_info_t chipInfo;
    esp_chip_info(&chipInfo);
    LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Device info: CPU cores: " + std::to_string(chipInfo.cores) + ", Chip revision: " + std::to_string(chipInfo.revision));

    // Print SD-Card info
    // ********************************************
    LogFile.WriteToFile(ESP_LOG_INFO, TAG, "SD card info: Name: " + getSDCardName() + ", Capacity: " + getSDCardCapacity() + "MB, Free: " + getSDCardFreePartitionSpace() + "MB");

    xDelay = 2000 / portTICK_PERIOD_MS;
    ESP_LOGD(TAG, "main: sleep for: %ldms", (long)xDelay * CONFIG_FREERTOS_HZ / portTICK_PERIOD_MS);
    vTaskDelay(xDelay);

    if (network_config.connection_type != NETWORK_CONNECTION_WIFI_AP_SETUP)
    {
        // Check main init + start TFlite task
        // ********************************************
        if (getSystemStatus() == 0)
        {
            // No error flag is set
            LogFile.WriteToFile(ESP_LOG_INFO, TAG, "Initialization completed successfully");
            InitializeFlowTask();
        }
        else if (isSetSystemStatusFlag(SYSTEM_STATUS_CAM_FB_BAD) || isSetSystemStatusFlag(SYSTEM_STATUS_NTP_BAD))
        {
            // Non critical errors occured, we try to continue...
            LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "SystemStatus: " + getSystemStatusText());
            LogFile.WriteToFile(ESP_LOG_WARN, TAG, "Initialization completed with non-critical errors!");
            InitializeFlowTask();
        }
        else
        {
            // Any other error is critical and makes running the flow impossible. Init is going to abort.
            LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "SystemStatus: " + getSystemStatusText());
            LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Initialization failed. Flow task start aborted. Loading reduced web interface...");

            vTaskDelay(2000 / portTICK_PERIOD_MS);
        }
    }
}
