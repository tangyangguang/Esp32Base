#include "../Esp32BaseProfile.h"

#if ESP32BASE_ENABLE_HEALTH

#include "Esp32BaseHealth.h"
#include "../core/Esp32BaseLog.h"

#if ESP32BASE_ENABLE_BUS
#include "Esp32BaseBus.h"
#endif

namespace {
bool g_healthReady = false;
uint32_t g_healthLastHandleMs = 0;
uint32_t g_healthLastTickMs = 0;
uint32_t g_healthLastDebugLogMs = 0;
uint32_t g_healthLoopPeriodMaxMs = 0;
uint32_t g_healthTickLoopPeriodMaxMs = 0;
uint32_t g_healthDebugLoopPeriodMaxMs = 0;
}

bool Esp32BaseHealth::begin() {
    g_healthReady = true;
    g_healthLastHandleMs = millis();
    g_healthLastTickMs = g_healthLastHandleMs;
    g_healthLastDebugLogMs = g_healthLastHandleMs;
    return true;
}

void Esp32BaseHealth::handle() {
    if (!g_healthReady) {
        return;
    }
    const uint32_t now = millis();
    const uint32_t period = now - g_healthLastHandleMs;
    g_healthLastHandleMs = now;
    if (period > g_healthLoopPeriodMaxMs) {
        g_healthLoopPeriodMaxMs = period;
    }
    if (period > g_healthTickLoopPeriodMaxMs) {
        g_healthTickLoopPeriodMaxMs = period;
    }
    if (now - g_healthLastTickMs >= ESP32BASE_HEALTH_TICK_INTERVAL_MS) {
        g_healthLastTickMs = now;
        if (g_healthTickLoopPeriodMaxMs > ESP32BASE_HEALTH_LOOP_WARN_MS) {
            ESP32BASE_LOG_I("health", "loop_slow loopMax=%lu ms threshold=%lu ms",
                            static_cast<unsigned long>(g_healthTickLoopPeriodMaxMs),
                            static_cast<unsigned long>(ESP32BASE_HEALTH_LOOP_WARN_MS));
        } else {
            if (g_healthTickLoopPeriodMaxMs > g_healthDebugLoopPeriodMaxMs) {
                g_healthDebugLoopPeriodMaxMs = g_healthTickLoopPeriodMaxMs;
            }
#if ESP32BASE_HEALTH_DEBUG_LOG_INTERVAL_MS > 0
            if (now - g_healthLastDebugLogMs >= ESP32BASE_HEALTH_DEBUG_LOG_INTERVAL_MS) {
                g_healthLastDebugLogMs = now;
                ESP32BASE_LOG_D("health", "tick loopMax=%lu ms", static_cast<unsigned long>(g_healthDebugLoopPeriodMaxMs));
                g_healthDebugLoopPeriodMaxMs = 0;
            }
#endif
        }
        g_healthTickLoopPeriodMaxMs = 0;
#if ESP32BASE_ENABLE_BUS
        Esp32BaseBus::publish(EVENT_TICK, "");
#endif
    }
}

uint32_t Esp32BaseHealth::lastTickMs() {
    return g_healthLastTickMs;
}

uint32_t Esp32BaseHealth::loopPeriodMaxMs() {
    return g_healthLoopPeriodMaxMs;
}

#endif
