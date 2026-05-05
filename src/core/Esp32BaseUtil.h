#pragma once

#include <Arduino.h>
#include <stddef.h>

namespace esp32base_internal {

inline void copySafe(char* dst, size_t dstLen, const char* src) {
    if (!dst || dstLen == 0) {
        return;
    }
    if (!src) {
        dst[0] = '\0';
        return;
    }
    strlcpy(dst, src, dstLen);
}

}
