#include "ClassLogFile.h"
#include "../../include/defines.h"
#include "psram.h"

#include <cstring>
#include <stdint.h>
#include <stdio.h>
#include <limits.h>
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

static const char* TAG = "PSRAM";

namespace {
enum class SharedMemoryUser : uint8_t { None, TakeImage, Aligning, Digitization };
static const uint32_t STBI_SHARED_BLOCK_MAGIC = 0x53544249;
static const size_t STBI_ALIGNMENT = 16;
struct alignas(16) StbiSharedBlockHeader {
    size_t size;
    uint32_t magic;
    uint8_t reserved[STBI_ALIGNMENT - sizeof(size_t) - sizeof(uint32_t)];
};
static_assert(sizeof(StbiSharedBlockHeader) == STBI_ALIGNMENT, "STBI shared allocation header must preserve alignment");
static portMUX_TYPE sharedMemoryMux = portMUX_INITIALIZER_UNLOCKED;
static uint8_t* sharedRegion = nullptr;
static size_t allocatedBytesForSTBI = 0;
static SharedMemoryUser sharedMemoryUser = SharedMemoryUser::None;
static bool tensorArenaClaimed = false;
static bool modelMemoryClaimed = false;

static bool getSharedRegionSize(size_t* size) {
    if (size == nullptr) return false;
    const size_t arenaSize = static_cast<size_t>(TENSOR_ARENA_SIZE);
    const size_t modelSize = static_cast<size_t>(MAX_MODEL_SIZE);
    if (arenaSize == 0 || modelSize == 0 || arenaSize > SIZE_MAX - modelSize) return false;
    *size = arenaSize + modelSize;
    return true;
}
static const char* sharedMemoryUserName(SharedMemoryUser user) {
    switch (user) {
        case SharedMemoryUser::TakeImage: return "TakeImage";
        case SharedMemoryUser::Aligning: return "Aligning";
        case SharedMemoryUser::Digitization: return "Digitization";
        default: return "";
    }
}
static bool isInsideSharedRegionLocked(const void* ptr, size_t regionSize) {
    if (ptr == nullptr || sharedRegion == nullptr) return false;
    const uintptr_t begin = reinterpret_cast<uintptr_t>(sharedRegion);
    const uintptr_t address = reinterpret_cast<uintptr_t>(ptr);
    return address >= begin && (address - begin) < regionSize;
}
static bool alignStbiOffset(size_t value, size_t* aligned) {
    if (aligned == nullptr || value > SIZE_MAX - (STBI_ALIGNMENT - 1)) return false;
    *aligned = (value + STBI_ALIGNMENT - 1) & ~(STBI_ALIGNMENT - 1);
    return true;
}
} // namespace

bool reserve_psram_shared_region(void) {
    size_t regionSize = 0;
    if (!getSharedRegionSize(&regionSize)) {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Invalid shared PSRAM region size");
        return false;
    }
    portENTER_CRITICAL(&sharedMemoryMux);
    const bool alreadyReserved = (sharedRegion != nullptr);
    portEXIT_CRITICAL(&sharedMemoryMux);
    if (alreadyReserved) return true;
    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "Allocating shared PSRAM region (" + std::to_string(regionSize) + " bytes)...");
    uint8_t* candidate = static_cast<uint8_t*>(malloc_psram_heap("Shared PSRAM region", regionSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (candidate == nullptr) {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Failed to allocate shared PSRAM region!");
        return false;
    }
    portENTER_CRITICAL(&sharedMemoryMux);
    if (sharedRegion == nullptr) { sharedRegion = candidate; candidate = nullptr; }
    portEXIT_CRITICAL(&sharedMemoryMux);
    if (candidate != nullptr) free_psram_heap("Unused duplicate shared PSRAM region", candidate);
    return true;
}

/*******************************************************************
 * Memory used in Take Image (STBI)
 *******************************************************************/
bool psram_init_shared_memory_for_take_image_step(void) {
    SharedMemoryUser currentUser;
    bool success = false;
    portENTER_CRITICAL(&sharedMemoryMux);
    currentUser = sharedMemoryUser;
    if (sharedRegion != nullptr && sharedMemoryUser == SharedMemoryUser::None) {
        allocatedBytesForSTBI = 0;
        sharedMemoryUser = SharedMemoryUser::TakeImage;
        success = true;
    }
    portEXIT_CRITICAL(&sharedMemoryMux);
    if (!success) {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, sharedRegion == nullptr ?
            "Shared PSRAM region is not initialized" :
            "Shared memory in PSRAM already in use for " + std::string(sharedMemoryUserName(currentUser)) + "!");
        return false;
    }
    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "Init shared memory for step 'Take Image' (STBI buffers)");
    return true;
}

void psram_deinit_shared_memory_for_take_image_step(void) {
    bool released = false;
    portENTER_CRITICAL(&sharedMemoryMux);
    if (sharedMemoryUser == SharedMemoryUser::TakeImage) {
        allocatedBytesForSTBI = 0;
        sharedMemoryUser = SharedMemoryUser::None;
        released = true;
    }
    portEXIT_CRITICAL(&sharedMemoryMux);
    if (released) LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "Deinit shared memory for step 'Take Image' (STBI buffers)");
    else LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Ignoring mismatched STBI shared-memory release");
}

void *psram_reserve_shared_stbi_memory(size_t size) {
    if (size == 0) return nullptr;
    if (size < 100000) return malloc_psram_heap("STBI", size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    size_t regionSize = 0;
    if (!getSharedRegionSize(&regionSize)) return nullptr;
    size_t userOffset = 0, endOffset = 0, available = 0;
    void* result = nullptr;
    SharedMemoryUser currentUser = SharedMemoryUser::None;
    portENTER_CRITICAL(&sharedMemoryMux);
    currentUser = sharedMemoryUser;
    if (sharedRegion != nullptr && sharedMemoryUser == SharedMemoryUser::TakeImage &&
        allocatedBytesForSTBI <= SIZE_MAX - sizeof(StbiSharedBlockHeader) &&
        alignStbiOffset(allocatedBytesForSTBI + sizeof(StbiSharedBlockHeader), &userOffset) &&
        userOffset <= regionSize && size <= regionSize - userOffset) {
        endOffset = userOffset + size;
        StbiSharedBlockHeader* header = reinterpret_cast<StbiSharedBlockHeader*>(sharedRegion + userOffset - sizeof(StbiSharedBlockHeader));
        header->size = size;
        header->magic = STBI_SHARED_BLOCK_MAGIC;
        std::memset(header->reserved, 0, sizeof(header->reserved));
        result = sharedRegion + userOffset;
        allocatedBytesForSTBI = endOffset;
    } else if (allocatedBytesForSTBI <= regionSize) available = regionSize - allocatedBytesForSTBI;
    portEXIT_CRITICAL(&sharedMemoryMux);
    if (result != nullptr) {
        LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "Allocated " + std::to_string(size) + " bytes in shared PSRAM for STBI");
        return result;
    }
    LogFile.WriteToFile(ESP_LOG_WARN, TAG, "Shared STBI PSRAM unavailable for " + std::to_string(size) +
        " bytes (owner: " + sharedMemoryUserName(currentUser) + ", available: " + std::to_string(available) +
        " bytes); falling back to PSRAM heap");
    return malloc_psram_heap("STBI fallback", size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

void *psram_reallocate_shared_stbi_memory(void *ptr, size_t newsize) {
    if (ptr == nullptr) return psram_reserve_shared_stbi_memory(newsize);
    if (newsize == 0) { psram_free_shared_stbi_memory(ptr); return nullptr; }
    size_t regionSize = 0;
    if (!getSharedRegionSize(&regionSize)) return nullptr;
    size_t oldSize = 0;
    bool sharedPointer = false, validSharedHeader = false;
    portENTER_CRITICAL(&sharedMemoryMux);
    sharedPointer = isInsideSharedRegionLocked(ptr, regionSize);
    if (sharedPointer) {
        const uintptr_t begin = reinterpret_cast<uintptr_t>(sharedRegion);
        const uintptr_t address = reinterpret_cast<uintptr_t>(ptr);
        if ((address - begin) >= sizeof(StbiSharedBlockHeader)) {
            const StbiSharedBlockHeader* header = reinterpret_cast<const StbiSharedBlockHeader*>(static_cast<const uint8_t*>(ptr) - sizeof(StbiSharedBlockHeader));
            if (header->magic == STBI_SHARED_BLOCK_MAGIC) { oldSize = header->size; validSharedHeader = true; }
        }
    }
    portEXIT_CRITICAL(&sharedMemoryMux);
    if (!sharedPointer) return realloc_psram_heap("STBI", ptr, newsize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!validSharedHeader) {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "STBI realloc received an unknown shared-region pointer");
        return nullptr;
    }
    void* replacement = psram_reserve_shared_stbi_memory(newsize);
    if (replacement == nullptr) return nullptr;
    std::memcpy(replacement, ptr, oldSize < newsize ? oldSize : newsize);
    return replacement;
}

void psram_free_shared_stbi_memory(void *ptr) {
    if (ptr == nullptr) return;
    size_t regionSize = 0;
    if (!getSharedRegionSize(&regionSize)) { free_psram_heap("STBI", ptr); return; }
    portENTER_CRITICAL(&sharedMemoryMux);
    const bool isShared = isInsideSharedRegionLocked(ptr, regionSize);
    portEXIT_CRITICAL(&sharedMemoryMux);
    if (!isShared) free_psram_heap("STBI", ptr);
}

/*******************************************************************
 * Memory used in Aligning Step
 *******************************************************************/
void *psram_reserve_shared_tmp_image_memory(void) {
    size_t regionSize = 0;
    if (!getSharedRegionSize(&regionSize) || static_cast<size_t>(IMAGE_SIZE) > regionSize) {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "tmpImage does not fit in the reserved shared PSRAM region");
        return nullptr;
    }
    SharedMemoryUser currentUser;
    void* result = nullptr;
    portENTER_CRITICAL(&sharedMemoryMux);
    currentUser = sharedMemoryUser;
    if (sharedRegion != nullptr && sharedMemoryUser == SharedMemoryUser::None) {
        sharedMemoryUser = SharedMemoryUser::Aligning;
        result = sharedRegion;
    }
    portEXIT_CRITICAL(&sharedMemoryMux);
    if (result == nullptr) {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Shared memory in PSRAM unavailable for tmpImage (owner: " +
            std::string(sharedMemoryUserName(currentUser)) + ")");
        return nullptr;
    }
    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "Allocating tmpImage (" + std::to_string(IMAGE_SIZE) + " bytes, use shared memory in PSRAM)...");
    return result;
}

void psram_free_shared_temp_image_memory(void) {
    bool released = false;
    portENTER_CRITICAL(&sharedMemoryMux);
    if (sharedMemoryUser == SharedMemoryUser::Aligning) { sharedMemoryUser = SharedMemoryUser::None; released = true; }
    portEXIT_CRITICAL(&sharedMemoryMux);
    if (released) LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "Shared memory used for tmpImage is free again");
    else LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Ignoring mismatched tmpImage shared-memory release");
}

/*******************************************************************
 * Memory used in Digitization Steps
 *******************************************************************/
void *psram_get_shared_tensor_arena_memory(void) {
    bool unavailable = false, duplicateClaim = false;
    void* result = nullptr;
    SharedMemoryUser currentUser;
    portENTER_CRITICAL(&sharedMemoryMux);
    currentUser = sharedMemoryUser;
    if (sharedRegion == nullptr || (sharedMemoryUser != SharedMemoryUser::None && sharedMemoryUser != SharedMemoryUser::Digitization)) unavailable = true;
    else if (tensorArenaClaimed) duplicateClaim = true;
    else { sharedMemoryUser = SharedMemoryUser::Digitization; tensorArenaClaimed = true; result = sharedRegion; }
    portEXIT_CRITICAL(&sharedMemoryMux);
    if (result == nullptr) {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, duplicateClaim ? "Tensor Arena shared memory was claimed more than once" :
            (unavailable ? "Shared PSRAM is unavailable for Tensor Arena (owner: " + std::string(sharedMemoryUserName(currentUser)) + ")" : "Tensor Arena allocation failed"));
        return nullptr;
    }
    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "Allocating Tensor Arena (" + std::to_string(TENSOR_ARENA_SIZE) + " bytes in shared PSRAM)...");
    return result;
}

void *psram_get_shared_model_memory(void) {
    bool unavailable = false, duplicateClaim = false;
    void* result = nullptr;
    SharedMemoryUser currentUser;
    portENTER_CRITICAL(&sharedMemoryMux);
    currentUser = sharedMemoryUser;
    if (sharedRegion == nullptr || (sharedMemoryUser != SharedMemoryUser::None && sharedMemoryUser != SharedMemoryUser::Digitization)) unavailable = true;
    else if (modelMemoryClaimed) duplicateClaim = true;
    else { sharedMemoryUser = SharedMemoryUser::Digitization; modelMemoryClaimed = true; result = sharedRegion + static_cast<size_t>(TENSOR_ARENA_SIZE); }
    portEXIT_CRITICAL(&sharedMemoryMux);
    if (result == nullptr) {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, duplicateClaim ? "Model shared memory was claimed more than once" :
            (unavailable ? "Shared PSRAM is unavailable for Model (owner: " + std::string(sharedMemoryUserName(currentUser)) + ")" : "Model allocation failed"));
        return nullptr;
    }
    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "Allocating Model memory (" + std::to_string(MAX_MODEL_SIZE) + " bytes in shared PSRAM)...");
    return result;
}

void psram_free_shared_tensor_arena_and_model_memory(void) {
    bool released = false;
    portENTER_CRITICAL(&sharedMemoryMux);
    if (sharedMemoryUser == SharedMemoryUser::Digitization) {
        tensorArenaClaimed = false; modelMemoryClaimed = false;
        sharedMemoryUser = SharedMemoryUser::None; released = true;
    }
    portEXIT_CRITICAL(&sharedMemoryMux);
    if (released) LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "Shared memory used for Tensor Arena and model is free again");
    else LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Ignoring mismatched Tensor Arena/Model shared-memory release");
}

/*******************************************************************
 * General PSRAM heap wrappers
 *******************************************************************/
void *malloc_psram_heap(std::string name, size_t size, uint32_t caps) {
    if (size == 0) {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Refusing zero-byte PSRAM allocation for '" + name + "'");
        return nullptr;
    }
    void* ptr = heap_caps_malloc(size, caps | MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ptr != nullptr) LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "Allocated " + std::to_string(size) + " bytes in PSRAM for '" + name + "'");
    else LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Failed to allocate " + std::to_string(size) + " bytes in PSRAM for '" + name + "'!");
    return ptr;
}

void *realloc_psram_heap(std::string name, void *ptr, size_t size, uint32_t caps) {
    void* resized = heap_caps_realloc(ptr, size, caps | MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (resized != nullptr) LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "Reallocated " + std::to_string(size) + " bytes in PSRAM for '" + name + "'");
    else if (size != 0) LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Failed to reallocate " + std::to_string(size) + " bytes in PSRAM for '" + name + "'!");
    return resized;
}

void *calloc_psram_heap(std::string name, size_t n, size_t size, uint32_t caps) {
    if (size != 0 && n > SIZE_MAX / size) {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "PSRAM calloc size overflow for '" + name + "'");
        return nullptr;
    }
    const size_t totalSize = n * size;
    if (totalSize == 0) {
        LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Refusing zero-byte PSRAM calloc for '" + name + "'");
        return nullptr;
    }
    void* ptr = heap_caps_calloc(n, size, caps | MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ptr != nullptr) LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "Allocated and cleared " + std::to_string(totalSize) + " bytes in PSRAM for '" + name + "'");
    else LogFile.WriteToFile(ESP_LOG_ERROR, TAG, "Failed to allocate " + std::to_string(totalSize) + " bytes in PSRAM for '" + name + "'!");
    return ptr;
}

void free_psram_heap(std::string name, void *ptr) {
    if (ptr == nullptr) return;
    LogFile.WriteToFile(ESP_LOG_DEBUG, TAG, "Freeing memory in PSRAM used for '" + name + "'...");
    heap_caps_free(ptr);
}
