#pragma once
#include <stddef.h>
#include <stdint.h>
inline void esp_fill_random(void* buffer, size_t length) {
    static uint32_t sequence = 1;
    auto* bytes = static_cast<uint8_t*>(buffer);
    for (size_t i = 0; i < length; ++i) bytes[i] = static_cast<uint8_t>(++sequence);
}
