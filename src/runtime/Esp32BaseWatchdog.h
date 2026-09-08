#pragma once

#include <Arduino.h>
#include <stdint.h>

class Esp32BaseWatchdog {
public:
    // Register the calling system task; reuse existing SDK watchdog policy.
    // Call serially from that task. See docs/03_api.md for global timeout ownership.
    static bool begin();
    static void feed();
    static bool enterLongOperation();
    static bool exitLongOperation();
    static bool currentTaskInLongOperation();
    static bool isEnabled();
    static bool wasWatchdogReset();
    static uint32_t lifetimeResetCount();
};
