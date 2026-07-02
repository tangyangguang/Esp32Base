#pragma once

#include "Esp32BaseProfile.h"
#include "core/Esp32BaseLog.h"
#include "core/Esp32BaseConfig.h"
#include "core/Esp32BaseSystem.h"

#ifndef ESP32BASE_DEFAULT_HOSTNAME
#define ESP32BASE_DEFAULT_HOSTNAME "esp32base"
#endif

#if ESP32BASE_ENABLE_BUS
#include "runtime/Esp32BaseBus.h"
#endif
#if ESP32BASE_ENABLE_WATCHDOG
#include "runtime/Esp32BaseWatchdog.h"
#endif
#if ESP32BASE_ENABLE_SLEEP
#include "runtime/Esp32BaseSleep.h"
#endif
#if ESP32BASE_ENABLE_FS
#include "runtime/Esp32BaseFs.h"
#endif
#if ESP32BASE_ENABLE_APP_EVENTS
#include "runtime/Esp32BaseAppEventLog.h"
#endif
#if ESP32BASE_ENABLE_FILELOG
#include "runtime/Esp32BaseFileLog.h"
#endif
#if ESP32BASE_ENABLE_RS485_PORT
#include "runtime/Esp32BaseRs485Port.h"
#endif
#if ESP32BASE_ENABLE_TIME
#include "runtime/Esp32BaseTime.h"
#endif
#if ESP32BASE_ENABLE_RTC
#include "runtime/Esp32BaseRtc.h"
#endif
#if ESP32BASE_ENABLE_HEALTH
#include "runtime/Esp32BaseHealth.h"
#endif
#if ESP32BASE_ENABLE_WIFI
#include "network/Esp32BaseWiFi.h"
#endif
#if ESP32BASE_ENABLE_DNS
#include "network/Esp32BaseDns.h"
#endif
#if ESP32BASE_ENABLE_NTP
#include "network/Esp32BaseNtp.h"
#endif
#if ESP32BASE_ENABLE_MDNS
#include "network/Esp32BaseMdns.h"
#endif
#if ESP32BASE_ENABLE_WEB
#include "web/Esp32BaseWeb.h"
#endif
#if ESP32BASE_ENABLE_APP_CONFIG
#include "web/Esp32BaseAppConfig.h"
#endif
#if ESP32BASE_ENABLE_OTA
#include "update/Esp32BaseOta.h"
#endif

class Esp32Base {
public:
    static bool begin();
    static void handle();

    static void setFirmwareInfo(const char* name, const char* version, const char* build = nullptr);
    static const char* firmwareName();
    static const char* firmwareVersion();
    static const char* firmwareBuild();

    static const char* hostname();
    static const char* defaultHostname();
    static bool isValidHostname(const char* hostname);

    static const char* profileName();
    static bool isReady();
    static const char* lastError();

    static void logStartupConfig();
    static void logResources();
};
