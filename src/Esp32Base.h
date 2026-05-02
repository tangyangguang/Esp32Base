#ifndef ESP32_BASE_H
#define ESP32_BASE_H

#include <stddef.h>

#include "Esp32BaseConfig.h"
#include "Esp32BaseLog.h"
#include "Esp32BaseNetwork.h"
#include "Esp32BaseRuntime.h"
#include "Esp32BaseWeb.h"

#ifndef ESP32BASE_HOSTNAME_LEN
#define ESP32BASE_HOSTNAME_LEN 32
#endif

#ifndef ESP32BASE_FIRMWARE_NAME_LEN
#define ESP32BASE_FIRMWARE_NAME_LEN 32
#endif

#ifndef ESP32BASE_FIRMWARE_VERSION_LEN
#define ESP32BASE_FIRMWARE_VERSION_LEN 16
#endif

#ifndef ESP32BASE_FIRMWARE_BUILD_LEN
#define ESP32BASE_FIRMWARE_BUILD_LEN 24
#endif

class Esp32Base {
public:
    static bool begin();
    static void handle();

    static void setFirmwareInfo(const char* name, const char* version, const char* build = nullptr);
    static void setHostname(const char* hostname);
    static const char* hostname();

    static const char* firmwareName();
    static const char* firmwareVersion();
    static const char* firmwareBuild();

    static bool isReady();
    static const char* lastError();

    static void logStartupConfig();
    static void logResources();

private:
    static void copyText(const char* in, char* out, size_t len);
    static bool fail(const char* message);

    static bool _ready;
    static bool _startupLogged;
    static const char* _lastError;
    static char _hostname[ESP32BASE_HOSTNAME_LEN];
    static char _firmwareName[ESP32BASE_FIRMWARE_NAME_LEN];
    static char _firmwareVersion[ESP32BASE_FIRMWARE_VERSION_LEN];
    static char _firmwareBuild[ESP32BASE_FIRMWARE_BUILD_LEN];
};

#endif
