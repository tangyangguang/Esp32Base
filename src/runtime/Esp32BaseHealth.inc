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
uint32_t g_healthLoopPeriodMaxMs = 0;
}

bool Esp32BaseHealth::begin() {
    g_healthReady = true;
    g_healthLastHandleMs = millis();
    g_healthLastTickMs = g_healthLastHandleMs;
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
    if (now - g_healthLastTickMs >= ESP32BASE_HEALTH_TICK_INTERVAL_MS) {
        g_healthLastTickMs = now;
        ESP32BASE_LOG_I("health", "tick loopMax=%lu ms", static_cast<unsigned long>(g_healthLoopPeriodMaxMs));
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
