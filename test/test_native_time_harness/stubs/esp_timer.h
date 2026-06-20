#pragma once

#include <cstdint>

extern int64_t g_nativeEspTimerUs;

inline int64_t esp_timer_get_time() {
    return g_nativeEspTimerUs;
}
