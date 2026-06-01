#pragma once

#include <Arduino.h>
#include <stdint.h>

class Esp32BaseWatchdog {
public:
    static bool begin(uint32_t timeoutMs);
    static void feed();
    static bool enterLongOperation();
    static bool exitLongOperation();
    static bool currentTaskInLongOperation();
    static bool isEnabled();
    static bool wasWatchdogReset();
    static uint32_t lifetimeResetCount();
};
