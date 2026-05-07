#include "Esp32Base.h"

#include "core/Esp32BaseUtil.h"

#include <string.h>

namespace {
bool g_ready = false;
bool g_startupLogged = false;
char g_firmwareName[33] = "app";
char g_firmwareVersion[17] = "0.0.0";
char g_firmwareBuild[33] = "";
char g_hostname[33] = "esp32base";
char g_lastError[96] = "";

bool optionalOk(bool ok, const char* module) {
    if (ok) {
        return true;
    }
    esp32base_internal::copySafe(g_lastError, sizeof(g_lastError), module);
    ESP32BASE_LOG_E("base", "module begin failed: %s", module);
#if defined(ESP32BASE_STRICT_OPTIONAL_BEGIN) && ESP32BASE_STRICT_OPTIONAL_BEGIN
    return false;
#else
    return true;
#endif
}

void logBootSessionStart() {
    ESP32BASE_LOG_I("boot", "============================================================");
    ESP32BASE_LOG_I("boot",
                    "BOOT SESSION START boot_count=%lu",
                    static_cast<unsigned long>(Esp32BaseSystem::bootCount()));
    ESP32BASE_LOG_I("boot",
                    "reset_reason=%s reset_desc=%s wake_reason=%s wake_desc=%s",
                    Esp32BaseSystem::resetReason(),
                    Esp32BaseSystem::resetReasonText(),
                    Esp32BaseSystem::wakeReason(),
                    Esp32BaseSystem::wakeReasonText());
    ESP32BASE_LOG_I("boot",
                    "firmware=%s version=%s build=%s profile=%s hostname=%s",
                    Esp32Base::firmwareName(),
                    Esp32Base::firmwareVersion(),
                    Esp32Base::firmwareBuild()[0] ? Esp32Base::firmwareBuild() : "-",
                    Esp32Base::profileName(),
                    Esp32Base::hostname());
    ESP32BASE_LOG_I("boot",
                    "free_heap=%lu min_heap=%lu flash=%lu",
                    static_cast<unsigned long>(Esp32BaseSystem::freeHeap()),
                    static_cast<unsigned long>(Esp32BaseSystem::minFreeHeap()),
                    static_cast<unsigned long>(Esp32BaseSystem::flashSize()));
    ESP32BASE_LOG_I("boot", "============================================================");
}
}

bool Esp32Base::begin() {
    g_lastError[0] = '\0';

    if (!Esp32BaseLog::begin()) {
        esp32base_internal::copySafe(g_lastError, sizeof(g_lastError), "log");
        return false;
    }
    if (!Esp32BaseConfig::begin()) {
        esp32base_internal::copySafe(g_lastError, sizeof(g_lastError), "config");
        return false;
    }
    if (!Esp32BaseSystem::begin()) {
        esp32base_internal::copySafe(g_lastError, sizeof(g_lastError), "system");
        return false;
    }

#if ESP32BASE_ENABLE_BUS
    if (!optionalOk(Esp32BaseBus::begin(), "bus")) return false;
#endif
#if ESP32BASE_ENABLE_FS
    optionalOk(Esp32BaseFs::begin(), "fs");
#endif
#if ESP32BASE_ENABLE_FILELOG
    optionalOk(Esp32BaseFileLog::begin(), "filelog");
#endif
#if ESP32BASE_ENABLE_WATCHDOG
    optionalOk(Esp32BaseWatchdog::begin(8000), "watchdog");
#endif
#if ESP32BASE_ENABLE_SLEEP
    optionalOk(Esp32BaseSleep::begin(), "sleep");
#endif
#if ESP32BASE_ENABLE_HEALTH
    optionalOk(Esp32BaseHealth::begin(), "health");
#endif
#if ESP32BASE_ENABLE_WIFI
    optionalOk(Esp32BaseWiFi::begin(), "wifi");
#endif

    g_ready = true;
    logBootSessionStart();
    ESP32BASE_LOG_I("base", "begin complete profile=%s", profileName());
    return true;
}

void Esp32Base::handle() {
#if ESP32BASE_ENABLE_OTA
    if (!Esp32BaseOta::isUploading()) {
        Esp32BaseConfig::handle();
    }
#else
    Esp32BaseConfig::handle();
#endif
#if ESP32BASE_ENABLE_FILELOG
    Esp32BaseFileLog::handle();
#endif

#if ESP32BASE_ENABLE_WIFI
    Esp32BaseWiFi::handle();
#endif
#if ESP32BASE_ENABLE_DNS
    if (!Esp32BaseDns::isRunning()) {
        Esp32BaseDns::begin();
    }
    Esp32BaseDns::handle();
#endif
#if ESP32BASE_ENABLE_WEB
    if (!Esp32BaseWeb::isReady() &&
        (Esp32BaseWiFi::isConnected() || Esp32BaseWiFi::state() == Esp32BaseWiFi::CONFIG_PORTAL)) {
        Esp32BaseWeb::begin();
    }
#endif
#if ESP32BASE_ENABLE_NTP
    if (Esp32BaseWiFi::isConnected() && !Esp32BaseNtp::isStarted()) {
        Esp32BaseNtp::begin();
    }
    if (Esp32BaseNtp::isStarted()) {
        Esp32BaseNtp::isTimeSynced();
    }
#endif
#if ESP32BASE_ENABLE_MDNS
    if (Esp32BaseWiFi::isConnected() && !Esp32BaseMdns::isRunning()) {
        Esp32BaseMdns::begin();
        Esp32BaseMdns::addHttpService(80);
    } else if (!Esp32BaseWiFi::isConnected() && Esp32BaseMdns::isRunning()) {
        Esp32BaseMdns::stop();
    }
#endif
#if ESP32BASE_ENABLE_OTA
    if (Esp32BaseWeb::isReady() && !Esp32BaseOta::isReady()) {
        Esp32BaseOta::begin();
    }
#endif
#if ESP32BASE_ENABLE_WEB
    Esp32BaseWeb::handle();
#endif
#if ESP32BASE_ENABLE_OTA
    Esp32BaseOta::handle();
#endif
#if ESP32BASE_ENABLE_HEALTH
    Esp32BaseHealth::handle();
#endif
#if ESP32BASE_ENABLE_WATCHDOG
    Esp32BaseWatchdog::feed();
#endif
    if (!g_startupLogged && g_ready) {
        logStartupConfig();
        logResources();
        g_startupLogged = true;
    }
}

void Esp32Base::setFirmwareInfo(const char* name, const char* version, const char* build) {
    esp32base_internal::copySafe(g_firmwareName, sizeof(g_firmwareName), name);
    esp32base_internal::copySafe(g_firmwareVersion, sizeof(g_firmwareVersion), version);
    esp32base_internal::copySafe(g_firmwareBuild, sizeof(g_firmwareBuild), build ? build : "");
}

const char* Esp32Base::firmwareName() {
    return g_firmwareName;
}

const char* Esp32Base::firmwareVersion() {
    return g_firmwareVersion;
}

const char* Esp32Base::firmwareBuild() {
    return g_firmwareBuild;
}

void Esp32Base::setHostname(const char* hostnameValue) {
    esp32base_internal::copySafe(g_hostname, sizeof(g_hostname), hostnameValue);
}

const char* Esp32Base::hostname() {
    return g_hostname;
}

const char* Esp32Base::profileName() {
#if ESP32BASE_PROFILE == ESP32BASE_PROFILE_CORE
    return "CORE";
#elif ESP32BASE_PROFILE == ESP32BASE_PROFILE_RUNTIME
    return "RUNTIME";
#elif ESP32BASE_PROFILE == ESP32BASE_PROFILE_NET
    return "NET";
#elif ESP32BASE_PROFILE == ESP32BASE_PROFILE_NET_RUNTIME
    return "NET_RUNTIME";
#elif ESP32BASE_PROFILE == ESP32BASE_PROFILE_WEB
    return "WEB";
#elif ESP32BASE_PROFILE == ESP32BASE_PROFILE_WEB_RUNTIME
    return "WEB_RUNTIME";
#elif ESP32BASE_PROFILE == ESP32BASE_PROFILE_FULL
    return "FULL";
#else
    return "CUSTOM";
#endif
}

bool Esp32Base::isReady() {
    return g_ready;
}

const char* Esp32Base::lastError() {
    return g_lastError;
}

void Esp32Base::logStartupConfig() {
    ESP32BASE_LOG_I("base", "firmware=%s version=%s build=%s", firmwareName(), firmwareVersion(), firmwareBuild());
    ESP32BASE_LOG_I("base", "hostname=%s profile=%s", hostname(), profileName());
}

void Esp32Base::logResources() {
    char freeHeap[48];
    char flash[48];
    Esp32BaseLog::formatBytes(Esp32BaseSystem::freeHeap(), freeHeap, sizeof(freeHeap));
    Esp32BaseLog::formatBytes(Esp32BaseSystem::flashSize(), flash, sizeof(flash));
    ESP32BASE_LOG_I("base", "freeHeap=%s flash=%s", freeHeap, flash);
}

#if ESP32BASE_ENABLE_BUS
#include "runtime/Esp32BaseBus.inc"
#endif
#if ESP32BASE_ENABLE_WATCHDOG
#include "runtime/Esp32BaseWatchdog.inc"
#endif
#if ESP32BASE_ENABLE_SLEEP
#include "runtime/Esp32BaseSleep.inc"
#endif
#if ESP32BASE_ENABLE_FS
#include "runtime/Esp32BaseFs.inc"
#endif
#if ESP32BASE_ENABLE_FILELOG
#include "runtime/Esp32BaseFileLog.inc"
#endif
#if ESP32BASE_ENABLE_HEALTH
#include "runtime/Esp32BaseHealth.inc"
#endif
#if ESP32BASE_ENABLE_WIFI
#include "network/Esp32BaseWiFi.inc"
#endif
#if ESP32BASE_ENABLE_DNS
#include "network/Esp32BaseDns.inc"
#endif
#if ESP32BASE_ENABLE_NTP
#include "network/Esp32BaseNtp.inc"
#endif
#if ESP32BASE_ENABLE_MDNS
#include "network/Esp32BaseMdns.inc"
#endif
#if ESP32BASE_ENABLE_WEB
#include "web/Esp32BaseWeb.inc"
#endif
#if ESP32BASE_ENABLE_OTA
#include "update/Esp32BaseOta.inc"
#endif
