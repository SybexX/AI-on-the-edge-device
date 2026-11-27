#pragma once

#include "../../include/defines.h"

#ifdef DEBUG_ENABLE_SYSINFO

#ifndef ESP_SYS_H
#define ESP_SYS_H


#include <string>


// Device libraries (ESP-IDF)
#include <esp_system.h>
#include <esp_spi_flash.h>
#include <esp_heap_caps.h>

// for esp_psram_get_size
extern "C" {
    #include "esp_psram.h"
    #ifdef USE_HIMEM_IF_AVAILABLE
        #include <esp32/himem.h>
    #endif
}



    void Restart();
    char *GetChipModel();

    uint32_t GetFreeHeap();
    uint32_t GetLeastHeapFreeSinceBoot();

    std::string get_device_info();

    #ifdef USE_HIMEM_IF_AVAILABLE
        size_t getHimemTotSpace();
        size_t getHimemFreeSpace();
        size_t getHimemReservedArea();
    #endif


#endif //ESP_SYS_H

#endif // DEBUG_ENABLE_SYSINFO
