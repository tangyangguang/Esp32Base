#pragma once

#include <Arduino.h>
#include <stdint.h>

#ifndef ESP32BASE_HEALTH_TICK_INTERVAL_MS
#define ESP32BASE_HEALTH_TICK_INTERVAL_MS 30000
#endif

#ifndef ESP32BASE_HEALTH_DEBUG_LOG_INTERVAL_MS
#define ESP32BASE_HEALTH_DEBUG_LOG_INTERVAL_MS 1800000UL
#endif

#ifndef ESP32BASE_HEALTH_LOOP_WARN_MS
#define ESP32BASE_HEALTH_LOOP_WARN_MS 3000
#endif

class Esp32BaseHealth {
public:
    static constexpr const char* EVENT_TICK = "health.tick";

    static bool begin();
    static void handle();
    static uint32_t lastTickMs();
    static uint32_t loopPeriodMaxMs();
};
