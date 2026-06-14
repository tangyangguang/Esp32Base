#pragma once

#include <stddef.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace esp32base_web {

inline void formatOtaPreflightBytes(size_t bytes, char* out, size_t len) {
    if (!out || len == 0) {
        return;
    }
    if (bytes >= 1024UL * 1024UL) {
        snprintf(out, len, "%.2f MB", static_cast<double>(bytes) / 1024.0 / 1024.0);
    } else if (bytes >= 1024UL) {
        snprintf(out, len, "%.2f KB", static_cast<double>(bytes) / 1024.0);
    } else {
        snprintf(out, len, "%lu B", static_cast<unsigned long>(bytes));
    }
}

inline bool parseOtaDeclaredSize(const char* text, size_t& out, char* error, size_t errorLen) {
    out = 0;
    if (error && errorLen) {
        error[0] = '\0';
    }
    if (!text || !*text) {
        if (error && errorLen) {
            snprintf(error, errorLen, "missing firmware size");
        }
        return false;
    }
    for (const char* p = text; *p; ++p) {
        if (!isdigit(static_cast<unsigned char>(*p))) {
            if (error && errorLen) {
                snprintf(error, errorLen, "invalid firmware size");
            }
            return false;
        }
    }
    errno = 0;
    char* end = nullptr;
    const unsigned long parsed = strtoul(text, &end, 10);
    if (errno != 0 || !end || *end != '\0' || parsed == 0UL) {
        if (error && errorLen) {
            snprintf(error, errorLen, "invalid firmware size");
        }
        return false;
    }
    out = static_cast<size_t>(parsed);
    if (static_cast<unsigned long>(out) != parsed) {
        if (error && errorLen) {
            snprintf(error, errorLen, "firmware size overflow");
        }
        return false;
    }
    return true;
}

inline bool validateOtaDeclaredSize(size_t firmwareSize, size_t partitionSize, char* error, size_t errorLen) {
    if (error && errorLen) {
        error[0] = '\0';
    }
    if (firmwareSize == 0) {
        if (error && errorLen) {
            snprintf(error, errorLen, "invalid ota size");
        }
        return false;
    }
    if (partitionSize == 0) {
        if (error && errorLen) {
            snprintf(error, errorLen, "no ota update partition");
        }
        return false;
    }
    if (firmwareSize > partitionSize) {
        char firmwareBuf[32];
        char partitionBuf[32];
        formatOtaPreflightBytes(firmwareSize, firmwareBuf, sizeof(firmwareBuf));
        formatOtaPreflightBytes(partitionSize, partitionBuf, sizeof(partitionBuf));
        if (error && errorLen) {
            snprintf(error, errorLen, "ota size too large: firmware=%s partition=%s", firmwareBuf, partitionBuf);
        }
        return false;
    }
    return true;
}

} // namespace esp32base_web
