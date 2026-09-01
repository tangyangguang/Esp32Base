#include "Esp32Base.h"

#include "core/Esp32BaseUtil.h"

#include <Arduino.h>
#include <string.h>

namespace {
bool g_ready = false;
bool g_startupLogged = false;
bool g_webStartDebugLogged = false;
bool g_ntpStartDebugLogged = false;
#if ESP32BASE_ENABLE_NTP
bool g_ntpSyncObserved = false;
#endif
bool g_mdnsStartDebugLogged = false;
char g_firmwareName[33] = "app";
char g_firmwareVersion[17] = "1.0.0";
char g_firmwareBuild[33] = "";
char g_defaultHostname[33] = "esp32base";
char g_hostname[33] = "esp32base";
char g_lastError[96] = "";
Esp32Base::BeforeNetworkStopCallback g_beforeNetworkStopCallback = nullptr;
void* g_beforeNetworkStopContext = nullptr;
#if ESP32BASE_ENABLE_MQTT && ESP32BASE_ENABLE_OTA
bool g_otaNetworkStopNotified = false;
#endif
constexpr uint16_t kMaximumNetworkStopGraceMs = 1000U;

bool hostnameValid(const char* hostname) {
    if (!hostname || !hostname[0]) {
        return false;
    }
    const size_t len = strlen(hostname);
    if (len == 0 || len > 32 || hostname[0] == '-' || hostname[len - 1] == '-') {
        return false;
    }
    for (const char* p = hostname; *p; ++p) {
        const char c = *p;
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-') {
            continue;
        }
        return false;
    }
    return true;
}

void initHostname() {
    if (hostnameValid(ESP32BASE_DEFAULT_HOSTNAME)) {
        esp32base_internal::copySafe(g_defaultHostname, sizeof(g_defaultHostname), ESP32BASE_DEFAULT_HOSTNAME);
    } else {
        esp32base_internal::copySafe(g_defaultHostname, sizeof(g_defaultHostname), "esp32base");
        ESP32BASE_LOG_W("base", "invalid default hostname ignored value=%s fallback=esp32base", ESP32BASE_DEFAULT_HOSTNAME);
    }
    esp32base_internal::copySafe(g_hostname, sizeof(g_hostname), g_defaultHostname);

    char stored[64] = "";
    if (!Esp32BaseConfig::getStr("eb_sys", "hostname", stored, sizeof(stored), "")) {
        ESP32BASE_LOG_D("base", "hostname source=default value=%s", g_hostname);
        return;
    }
    if (hostnameValid(stored)) {
        esp32base_internal::copySafe(g_hostname, sizeof(g_hostname), stored);
        ESP32BASE_LOG_I("base", "hostname loaded source=nvs value=%s default=%s", g_hostname, g_defaultHostname);
        return;
    }
    ESP32BASE_LOG_W("base", "invalid stored hostname ignored value=%s default=%s", stored, g_defaultHostname);
}

bool optionalOk(bool ok, const char* module) {
    if (ok) {
        return true;
    }
    esp32base_internal::copySafe(g_lastError, sizeof(g_lastError), module);
    ESP32BASE_LOG_E("base", "module begin failed: %s", module);
#if ESP32BASE_STRICT_OPTIONAL_BEGIN
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
    char freeHeap[48];
    char minHeap[48];
    char flash[48];
    Esp32BaseLog::formatBytes(Esp32BaseSystem::freeHeap(), freeHeap, sizeof(freeHeap));
    Esp32BaseLog::formatBytes(Esp32BaseSystem::minFreeHeap(), minHeap, sizeof(minHeap));
    Esp32BaseLog::formatBytes(Esp32BaseSystem::flashSize(), flash, sizeof(flash));
    ESP32BASE_LOG_I("boot",
                    "free_heap=%s min_heap=%s flash=%s",
                    freeHeap,
                    minHeap,
                    flash);
    ESP32BASE_LOG_I("boot", "============================================================");
}

}

uint16_t Esp32Base::notifyBeforeNetworkStop() {
    if (!g_beforeNetworkStopCallback) {
        return 0;
    }
    const uint16_t requested =
        g_beforeNetworkStopCallback(g_beforeNetworkStopContext);
    return requested > kMaximumNetworkStopGraceMs
               ? kMaximumNetworkStopGraceMs
               : requested;
}

void Esp32Base::prepareForLifecycleStop() {
    const uint16_t graceMs = notifyBeforeNetworkStop();
    if (graceMs > 0U) {
        delay(graceMs);
    }
#if ESP32BASE_ENABLE_MQTT
    Esp32BaseMqtt::prepareForLifecycleStop();
    if (graceMs > 0U) {
        delay(graceMs);
    }
#endif
#if ESP32BASE_ENABLE_FILELOG
    Esp32BaseFileLog::flush();
#endif
}

bool Esp32Base::begin() {
    g_lastError[0] = '\0';
    g_webStartDebugLogged = false;
    g_ntpStartDebugLogged = false;
#if ESP32BASE_ENABLE_NTP
    g_ntpSyncObserved = false;
#endif
    g_mdnsStartDebugLogged = false;

    if (!Esp32BaseLog::begin()) {
        esp32base_internal::copySafe(g_lastError, sizeof(g_lastError), "log");
        return false;
    }
    ESP32BASE_LOG_D("base", "module_ready name=log");
    ESP32BASE_LOG_D("base", "begin_start profile=%s", profileName());
    ESP32BASE_LOG_D("base", "module_begin name=config");
    if (!Esp32BaseConfig::begin()) {
        esp32base_internal::copySafe(g_lastError, sizeof(g_lastError), "config");
        return false;
    }
    ESP32BASE_LOG_D("base", "module_ready name=config");
    initHostname();
    ESP32BASE_LOG_D("base", "module_begin name=system");
    if (!Esp32BaseSystem::begin()) {
        esp32base_internal::copySafe(g_lastError, sizeof(g_lastError), "system");
        return false;
    }
    esp32base_internal::registerPreRestartHook(prepareForLifecycleStop);
    esp32base_internal::registerPreSleepHook(prepareForLifecycleStop);
    ESP32BASE_LOG_D("base", "module_ready name=system");
#if ESP32BASE_ENABLE_OTA
    // Keep ESP32BASE_OTA_REQUIRE_MARK_VALID rollback timing independent from WiFi/Web readiness.
    ESP32BASE_LOG_D("base", "module_begin name=ota_boot");
    if (!Esp32BaseOta::begin()) {
        esp32base_internal::copySafe(g_lastError, sizeof(g_lastError), "ota");
        return false;
    }
    ESP32BASE_LOG_D("base", "module_ready name=ota_boot");
#endif
#if ESP32BASE_ENABLE_TIME
    Esp32BaseTime::initBootSession();
#endif
#if ESP32BASE_ENABLE_RTC
    ESP32BASE_LOG_D("base", "module_begin name=rtc");
    {
        const bool ok = Esp32BaseRtc::begin();
        if (!optionalOk(ok, "rtc")) return false;
        if (ok) ESP32BASE_LOG_D("base", "module_ready name=rtc");
    }
#endif
#if ESP32BASE_ENABLE_NTP
    Esp32BaseNtp::initBootSession();
#endif
#if ESP32BASE_ENABLE_BUS
    ESP32BASE_LOG_D("base", "module_begin name=bus");
    {
        const bool ok = Esp32BaseBus::begin();
        if (!optionalOk(ok, "bus")) return false;
        if (ok) ESP32BASE_LOG_D("base", "module_ready name=bus");
    }
#endif
#if ESP32BASE_ENABLE_FS
    ESP32BASE_LOG_D("base", "module_begin name=fs");
    {
        const bool ok = Esp32BaseFs::begin();
        if (!optionalOk(ok, "fs")) return false;
        if (ok) ESP32BASE_LOG_D("base", "module_ready name=fs");
    }
#endif
#if ESP32BASE_ENABLE_APP_EVENTS
    ESP32BASE_LOG_D("base", "module_begin name=app_events");
    {
        const bool ok = Esp32BaseAppEvents::begin();
        if (!optionalOk(ok, "app_events")) return false;
        if (ok) ESP32BASE_LOG_D("base", "module_ready name=app_events");
    }
#endif
#if ESP32BASE_ENABLE_FILELOG
    ESP32BASE_LOG_D("base", "module_begin name=filelog");
    {
        const bool ok = Esp32BaseFileLog::begin();
        if (!optionalOk(ok, "filelog")) return false;
        if (ok) ESP32BASE_LOG_D("base", "module_ready name=filelog");
    }
#endif
#if ESP32BASE_ENABLE_WATCHDOG
    ESP32BASE_LOG_D("base", "module_begin name=watchdog");
    {
        const bool ok = Esp32BaseWatchdog::begin(8000);
        if (!optionalOk(ok, "watchdog")) return false;
        if (ok) ESP32BASE_LOG_D("base", "module_ready name=watchdog");
    }
#endif
#if ESP32BASE_ENABLE_SLEEP
    ESP32BASE_LOG_D("base", "module_begin name=sleep");
    {
        const bool ok = Esp32BaseSleep::begin();
        if (!optionalOk(ok, "sleep")) return false;
        if (ok) ESP32BASE_LOG_D("base", "module_ready name=sleep");
    }
#endif
#if ESP32BASE_ENABLE_HEALTH
    ESP32BASE_LOG_D("base", "module_begin name=health");
    {
        const bool ok = Esp32BaseHealth::begin();
        if (!optionalOk(ok, "health")) return false;
        if (ok) ESP32BASE_LOG_D("base", "module_ready name=health");
    }
#endif
#if ESP32BASE_ENABLE_WIFI
    ESP32BASE_LOG_D("base", "module_begin name=wifi");
    {
        const bool ok = Esp32BaseWiFi::begin();
        if (!optionalOk(ok, "wifi")) return false;
        if (ok) ESP32BASE_LOG_D("base", "module_ready name=wifi");
    }
#endif
#if ESP32BASE_ENABLE_MQTT
    ESP32BASE_LOG_D("base", "module_begin name=mqtt");
    {
        const bool ok = Esp32BaseMqtt::begin();
        if (!optionalOk(ok, "mqtt")) return false;
        if (ok) ESP32BASE_LOG_D("base", "module_ready name=mqtt");
    }
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
#if ESP32BASE_ENABLE_RTC
    Esp32BaseRtc::handle();
#endif

#if ESP32BASE_ENABLE_WIFI
    Esp32BaseWiFi::handle();
#endif
#if ESP32BASE_ENABLE_DNS
    if (Esp32BaseWiFi::state() == Esp32BaseWiFi::CONFIG_PORTAL && !Esp32BaseDns::isRunning()) {
        Esp32BaseDns::begin();
    } else if (Esp32BaseWiFi::state() != Esp32BaseWiFi::CONFIG_PORTAL && Esp32BaseDns::isRunning()) {
        Esp32BaseDns::stop();
    }
    Esp32BaseDns::handle();
#endif
#if ESP32BASE_ENABLE_WEB
    if (!Esp32BaseWeb::isReady() &&
        !Esp32BaseWeb::startLocked() &&
        (Esp32BaseWiFi::isConnected() || Esp32BaseWiFi::state() == Esp32BaseWiFi::CONFIG_PORTAL)) {
        if (!g_webStartDebugLogged) {
            ESP32BASE_LOG_D("base", "deferred_start module=web reason=%s",
                            Esp32BaseWiFi::isConnected() ? "wifi_connected" : "config_portal");
            g_webStartDebugLogged = true;
        }
        Esp32BaseWeb::begin();
    }
#endif
#if ESP32BASE_ENABLE_NTP
    if (Esp32BaseWiFi::isConnected() && !Esp32BaseNtp::isStarted()) {
        if (!g_ntpStartDebugLogged) {
            ESP32BASE_LOG_D("base", "deferred_start module=ntp reason=wifi_connected");
            g_ntpStartDebugLogged = true;
        }
        Esp32BaseNtp::begin();
    }
    if (Esp32BaseNtp::isStarted() && !g_ntpSyncObserved) {
        g_ntpSyncObserved = Esp32BaseNtp::isTimeSynced();
    }
#endif
#if ESP32BASE_ENABLE_MQTT
#if ESP32BASE_ENABLE_OTA
    const bool otaUploading = Esp32BaseOta::isUploading();
    if (otaUploading && !g_otaNetworkStopNotified) {
        const uint16_t graceMs = notifyBeforeNetworkStop();
        if (graceMs > 0U) {
            delay(graceMs);
        }
        g_otaNetworkStopNotified = true;
    } else if (!otaUploading) {
        g_otaNetworkStopNotified = false;
    }
    Esp32BaseMqtt::handle(otaUploading);
#else
    Esp32BaseMqtt::handle(false);
#endif
#endif
#if ESP32BASE_ENABLE_MDNS
    if (Esp32BaseWiFi::isConnected() && !Esp32BaseMdns::isRunning()) {
        if (!g_mdnsStartDebugLogged) {
            ESP32BASE_LOG_D("base", "deferred_start module=mdns reason=wifi_connected");
            g_mdnsStartDebugLogged = true;
        }
        Esp32BaseMdns::begin();
        Esp32BaseMdns::addHttpService(80);
    } else if (!Esp32BaseWiFi::isConnected() && Esp32BaseMdns::isRunning()) {
        ESP32BASE_LOG_D("base", "deferred_stop module=mdns reason=wifi_disconnected");
        g_mdnsStartDebugLogged = false;
        Esp32BaseMdns::stop();
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

void Esp32Base::setBeforeNetworkStopCallback(BeforeNetworkStopCallback callback,
                                             void* context) {
    g_beforeNetworkStopCallback = callback;
    g_beforeNetworkStopContext = context;
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

const char* Esp32Base::hostname() {
    return g_hostname;
}

const char* Esp32Base::defaultHostname() {
    return g_defaultHostname;
}

bool Esp32Base::isValidHostname(const char* hostnameValue) {
    return hostnameValid(hostnameValue);
}

const char* Esp32Base::profileName() {
#if ESP32BASE_PROFILE == ESP32BASE_PROFILE_MINIMAL
    return "MINIMAL";
#elif ESP32BASE_PROFILE == ESP32BASE_PROFILE_OFFLINE
    return "OFFLINE";
#elif ESP32BASE_PROFILE == ESP32BASE_PROFILE_LOCAL
    return "LOCAL";
#elif ESP32BASE_PROFILE == ESP32BASE_PROFILE_IOT
    return "IOT";
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
#if ESP32BASE_ENABLE_RECORD_STORE
#include "runtime/Esp32BaseRecordStore.inc"
#endif
#if ESP32BASE_ENABLE_APP_EVENTS
#include "runtime/Esp32BaseAppEvents.inc"
#endif
#if ESP32BASE_ENABLE_FILELOG
#include "runtime/Esp32BaseFileLog.inc"
#endif
#if ESP32BASE_ENABLE_RS485_PORT
#include "runtime/Esp32BaseRs485Port.inc"
#endif
#if ESP32BASE_ENABLE_TIME
#include "runtime/Esp32BaseTime.inc"
#endif
#if ESP32BASE_ENABLE_RTC
#include "runtime/Esp32BaseRtc.inc"
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
#if ESP32BASE_ENABLE_MQTT
#include "network/Esp32BaseMqtt.inc"
#endif
#if ESP32BASE_ENABLE_OTA
#include "update/Esp32BaseOta.inc"
#endif
