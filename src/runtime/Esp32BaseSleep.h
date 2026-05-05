#pragma once

#include <Arduino.h>
#include <stdint.h>

class Esp32BaseSleep {
public:
    static bool begin();
    static bool deepSleepSeconds(uint32_t seconds);
    static bool deepSleepUs(uint64_t us);
    static bool enableTimerWakeup(uint64_t us);
    static bool enableGpioWakeup(int gpio, int level);
};
