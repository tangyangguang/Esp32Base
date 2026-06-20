#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

extern uint32_t g_nativeMillis;

inline uint32_t millis() {
    return g_nativeMillis;
}

inline size_t strlcpy(char* dst, const char* src, size_t size) {
    const size_t srcLen = src ? std::strlen(src) : 0;
    if (size > 0) {
        const size_t copyLen = srcLen >= size ? size - 1 : srcLen;
        if (copyLen > 0 && src) {
            std::memcpy(dst, src, copyLen);
        }
        dst[copyLen] = '\0';
    }
    return srcLen;
}

struct NativeSerial {
    void begin(uint32_t) {}
    void println(const char*) {}
};

extern NativeSerial Serial;
