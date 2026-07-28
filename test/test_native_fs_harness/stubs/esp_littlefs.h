#pragma once

#include "LittleFS.h"

#include <cstddef>

constexpr int ESP_PARTITION_SUBTYPE_DATA_LITTLEFS = 0;
constexpr int ESP_OK = 0;

inline int esp_littlefs_info(const char*, size_t* total, size_t* used) {
    if (!total || !used) {
        return -1;
    }
    *total = LittleFS.totalBytes();
    *used = LittleFS.usedBytes();
    return 0;
}
