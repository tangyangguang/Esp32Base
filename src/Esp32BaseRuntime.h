#ifndef ESP32_BASE_RUNTIME_H
#define ESP32_BASE_RUNTIME_H

#include <stddef.h>
#include <stdint.h>

#ifndef ESP32BASE_WATCHDOG_TIMEOUT_MS
#define ESP32BASE_WATCHDOG_TIMEOUT_MS 10000UL
#endif

#ifndef ESP32BASE_FILE_READ_MAX
#define ESP32BASE_FILE_READ_MAX 4096
#endif

class Esp32BaseRuntime {
public:
    static bool begin();
    static void handle();
    static bool isReady();

    static const char* resetReason();
    static const char* wakeReason();

    static uint32_t freeHeap();
    static uint32_t totalHeap();
    static uint32_t flashSize();

    static void restart();
    static void deepSleepSeconds(uint32_t seconds);
    static void deepSleepUs(uint64_t us);

    static bool enableWatchdog(uint32_t timeoutMs = ESP32BASE_WATCHDOG_TIMEOUT_MS);
    static void disableWatchdog();
    static void feedWatchdog();
    static bool isWatchdogEnabled();
    static bool wasWatchdogReset();
    static uint32_t watchdogCount();
    static void clearWatchdogCount();

    static bool writeFile(const char* path, const char* content);
    static bool readFile(const char* path, char* out, size_t len);
    static bool fileExists(const char* path);
    static bool removeFile(const char* path);
    static size_t fsTotalBytes();
    static size_t fsUsedBytes();
    static size_t fsFreeBytes();

    static void logConfig();

private:
    static bool mountFileSystem();
    static void storeWatchdogCount(uint32_t count);

    static bool _ready;
    static bool _fsReady;
    static bool _watchdogEnabled;
    static uint32_t _watchdogTimeoutMs;
    static uint32_t _watchdogCount;
};

#endif
