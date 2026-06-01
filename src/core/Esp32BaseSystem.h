#pragma once

#include <Arduino.h>
#include <stdint.h>

class Esp32BaseSystem {
public:
    using PreLifecycleHook = void (*)();

    static bool begin();
    static bool isReady();

    static uint32_t freeHeap();
    static uint32_t minFreeHeap();
    static uint32_t totalHeap();
    static uint32_t flashSize();
    static uint32_t uptimeMs();
    static uint32_t bootCount();

    static const char* resetReason();
    static const char* resetReasonText();
    static const char* wakeReason();
    static const char* wakeReasonText();

    static void restart(const char* reason);
    static void setPreRestartHook(PreLifecycleHook hook);
    static void setPreSleepHook(PreLifecycleHook hook);
    static void runPreSleepHook();

    static bool appendRestartLog(const char* reason);
    static uint8_t restartLogCount();
};
