#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

inline uint32_t& nativeMillisValue() {
    static uint32_t value = 0;
    return value;
}

inline uint32_t millis() { return nativeMillisValue(); }
inline void yield() {}

inline size_t strlcpy(char* dst, const char* src, size_t size) {
    const size_t sourceLength = src ? std::strlen(src) : 0;
    if (size > 0) {
        const size_t copyLength = sourceLength >= size ? size - 1U : sourceLength;
        if (copyLength > 0 && src) std::memcpy(dst, src, copyLength);
        dst[copyLength] = '\0';
    }
    return sourceLength;
}
