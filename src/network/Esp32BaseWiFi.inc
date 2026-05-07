#include "../Esp32BaseProfile.h"

#if ESP32BASE_ENABLE_WIFI

#include "Esp32BaseWiFi.h"
#include "../core/Esp32BaseConfig.h"
#include "../core/Esp32BaseLog.h"

#if ESP32BASE_ENABLE_BUS
#include "../runtime/Esp32BaseBus.h"
#endif

#include <WiFi.h>
#include <string.h>

#ifndef ESP32BASE_WIFI_CONNECT_TIMEOUT_MS
#define ESP32BASE_WIFI_CONNECT_TIMEOUT_MS 15000
#endif
#ifndef ESP32BASE_WIFI_RETRY_FAST_MS
#define ESP32BASE_WIFI_RETRY_FAST_MS 5000
#endif
#ifndef ESP32BASE_WIFI_RETRY_FAST_COUNT
#define ESP32BASE_WIFI_RETRY_FAST_COUNT 3
#endif
#ifndef ESP32BASE_WIFI_RETRY_SLOW_MS
#define ESP32BASE_WIFI_RETRY_SLOW_MS 60000
#endif

namespace {
bool g_wifiReady = false;
Esp32BaseWiFi::State g_wifiState = Esp32BaseWiFi::IDLE;
char g_wifiSsid[33] = "";
char g_wifiPassword[65] = "";
char g_configPortalSsid[32] = "";
uint32_t g_wifiAttemptStartMs = 0;
uint32_t g_wifiNextRetryMs = 0;
uint8_t g_wifiRetryCount = 0;
bool g_wifiPowerSave = true;

bool credentialsPresent() {
    return g_wifiSsid[0] != '\0' && g_wifiPassword[0] != '\0';
}

bool credentialsValid(const char* ssidValue, const char* passwordValue) {
    return ssidValue && passwordValue &&
           ssidValue[0] != '\0' && passwordValue[0] != '\0' &&
           strlen(ssidValue) < sizeof(g_wifiSsid) &&
           strlen(passwordValue) < sizeof(g_wifiPassword);
}

const char* wlStatusName(wl_status_t status);

uint32_t retryDelayMs() {
    if (g_wifiRetryCount < ESP32BASE_WIFI_RETRY_FAST_COUNT) {
        return ESP32BASE_WIFI_RETRY_FAST_MS;
    }
    return ESP32BASE_WIFI_RETRY_SLOW_MS;
}

void publish(const char* event) {
#if ESP32BASE_ENABLE_BUS
    Esp32BaseBus::publish(event, "");
#else
    (void)event;
#endif
}

bool loadCredentials() {
    if (!Esp32BaseConfig::getStr("eb_wifi", "ssid", g_wifiSsid, sizeof(g_wifiSsid), "") ||
        !Esp32BaseConfig::getStr("eb_wifi", "pass", g_wifiPassword, sizeof(g_wifiPassword), "")) {
        g_wifiSsid[0] = '\0';
        g_wifiPassword[0] = '\0';
        return false;
    }
    return credentialsPresent();
}

void beginSta() {
    if (!credentialsPresent()) {
        g_wifiState = Esp32BaseWiFi::IDLE;
        ESP32BASE_LOG_W("wifi", "sta connect skipped: no credentials");
        return;
    }
    WiFi.mode(WIFI_STA);
    WiFi.begin(g_wifiSsid, g_wifiPassword);
    g_wifiAttemptStartMs = millis();
    g_wifiState = Esp32BaseWiFi::CONNECTING;
    ESP32BASE_LOG_I("wifi", "station_connecting ssid=%s password=%s status=%s status_code=%u",
                    g_wifiSsid,
                    g_wifiPassword,
                    wlStatusName(WiFi.status()),
                    static_cast<unsigned>(WiFi.status()));
}

void scheduleRetry(uint32_t now, const char* reason) {
    g_wifiState = Esp32BaseWiFi::RETRY_BACKOFF;
    const uint32_t delayMs = retryDelayMs();
    g_wifiNextRetryMs = now + delayMs;
    ESP32BASE_LOG_W("wifi", "%s attempt=%u retry_in=%lu ms mode=%s status=%s status_code=%u rssi=%ld",
                    reason,
                    static_cast<unsigned>(g_wifiRetryCount + 1),
                    static_cast<unsigned long>(delayMs),
                    g_wifiRetryCount < ESP32BASE_WIFI_RETRY_FAST_COUNT ? "fast" : "slow",
                    wlStatusName(WiFi.status()),
                    static_cast<unsigned>(WiFi.status()),
                    static_cast<long>(WiFi.RSSI()));
    publish(Esp32BaseWiFi::EVENT_RETRY);
}

const char* wlStatusName(wl_status_t status) {
    switch (status) {
        case WL_IDLE_STATUS: return "WL_IDLE_STATUS";
        case WL_NO_SSID_AVAIL: return "WL_NO_SSID_AVAIL";
        case WL_SCAN_COMPLETED: return "WL_SCAN_COMPLETED";
        case WL_CONNECTED: return "WL_CONNECTED";
        case WL_CONNECT_FAILED: return "WL_CONNECT_FAILED";
        case WL_CONNECTION_LOST: return "WL_CONNECTION_LOST";
        case WL_DISCONNECTED: return "WL_DISCONNECTED";
        default: return "WL_UNKNOWN";
    }
}
}

bool Esp32BaseWiFi::begin() {
    g_wifiReady = true;
    WiFi.persistent(false);
    if (loadCredentials()) {
        beginSta();
    } else {
        startConfigPortal();
    }
    return true;
}

void Esp32BaseWiFi::handle() {
    if (!g_wifiReady) {
        return;
    }
    const wl_status_t status = WiFi.status();
    const uint32_t now = millis();
    if ((g_wifiState == CONNECTING || g_wifiState == RETRY_BACKOFF) && status == WL_CONNECTED) {
        g_wifiState = CONNECTED;
        g_wifiRetryCount = 0;
        ESP32BASE_LOG_I("wifi", "connected ip=%s", WiFi.localIP().toString().c_str());
        publish(EVENT_CONNECTED);
        return;
    }
    if (g_wifiState == CONNECTED && status != WL_CONNECTED) {
        ESP32BASE_LOG_W("wifi", "station_disconnected status=%s status_code=%u rssi=%ld",
                        wlStatusName(status), static_cast<unsigned>(status), static_cast<long>(WiFi.RSSI()));
        publish(EVENT_DISCONNECTED);
        scheduleRetry(now, "connection lost");
        return;
    }
    if (g_wifiState == CONNECTING && status != WL_CONNECTED &&
        static_cast<int32_t>(now - g_wifiAttemptStartMs) >= ESP32BASE_WIFI_CONNECT_TIMEOUT_MS) {
        ESP32BASE_LOG_W("wifi", "station_connect_timeout ssid=%s status=%s status_code=%u elapsed=%lu ms rssi=%ld",
                        g_wifiSsid,
                        wlStatusName(status),
                        static_cast<unsigned>(status),
                        static_cast<unsigned long>(now - g_wifiAttemptStartMs),
                        static_cast<long>(WiFi.RSSI()));
        scheduleRetry(now, "connect timeout");
        return;
    }
    if (g_wifiState == RETRY_BACKOFF && status != WL_CONNECTED &&
        static_cast<int32_t>(now - g_wifiNextRetryMs) >= 0) {
        if (!credentialsPresent()) {
            g_wifiState = IDLE;
            ESP32BASE_LOG_W("wifi", "retry stopped: no credentials");
            return;
        }
        ++g_wifiRetryCount;
        beginSta();
    }
}

bool Esp32BaseWiFi::isReady() {
    return g_wifiReady;
}

bool Esp32BaseWiFi::connect(const char* ssidValue, const char* passwordValue, bool persist) {
    if (!credentialsValid(ssidValue, passwordValue)) {
        ESP32BASE_LOG_W("wifi", "reject invalid ssid/password");
        return false;
    }
    if (persist) {
        // Explicit credential saves stay synchronous so a reboot right after the form submit keeps the new WiFi config.
        if (!Esp32BaseConfig::setStr("eb_wifi", "ssid", ssidValue) ||
            !Esp32BaseConfig::setStr("eb_wifi", "pass", passwordValue)) {
            ESP32BASE_LOG_E("wifi", "credential save failed");
            return false;
        }
    }
    strlcpy(g_wifiSsid, ssidValue, sizeof(g_wifiSsid));
    strlcpy(g_wifiPassword, passwordValue, sizeof(g_wifiPassword));
    ESP32BASE_LOG_I("wifi", "credentials_set ssid=%s password=%s persist=%s",
                    g_wifiSsid,
                    g_wifiPassword,
                    persist ? "yes" : "no");
    g_wifiRetryCount = 0;
    stopConfigPortal();
    beginSta();
    return true;
}

bool Esp32BaseWiFi::clearCredentials() {
    g_wifiSsid[0] = '\0';
    g_wifiPassword[0] = '\0';
    Esp32BaseConfig::clearNamespace("eb_wifi");
    return true;
}

bool Esp32BaseWiFi::startConfigPortal() {
    WiFi.mode(WIFI_AP);
    g_wifiRetryCount = 0;
    if (!g_configPortalSsid[0]) {
        snprintf(g_configPortalSsid, sizeof(g_configPortalSsid), "ESP32-Config-%04X",
                 static_cast<unsigned>(static_cast<uint32_t>(ESP.getEfuseMac()) & 0xFFFFU));
    }
    const bool ok = WiFi.softAP(g_configPortalSsid);
    g_wifiState = ok ? CONFIG_PORTAL : FAILED;
    if (ok) {
        publish(EVENT_CONFIG_PORTAL);
        ESP32BASE_LOG_I("wifi", "config_portal_started ssid=%s ip=%s", g_configPortalSsid, WiFi.softAPIP().toString().c_str());
    }
    return ok;
}

bool Esp32BaseWiFi::stopConfigPortal() {
    if (g_wifiState == CONFIG_PORTAL) {
        WiFi.softAPdisconnect(true);
        g_wifiState = IDLE;
    }
    return true;
}

Esp32BaseWiFi::State Esp32BaseWiFi::state() {
    return g_wifiState;
}

const char* Esp32BaseWiFi::stateName() {
    switch (g_wifiState) {
        case IDLE: return "IDLE";
        case CONNECTING: return "CONNECTING";
        case CONNECTED: return "CONNECTED";
        case CONFIG_PORTAL: return "CONFIG_PORTAL";
        case RETRY_BACKOFF: return "RETRY_BACKOFF";
        case FAILED: return "FAILED";
        default: return "UNKNOWN";
    }
}

bool Esp32BaseWiFi::isConnected() {
    return WiFi.status() == WL_CONNECTED;
}

const char* Esp32BaseWiFi::ssid() {
    return g_wifiSsid;
}

bool Esp32BaseWiFi::ip(char* out, size_t len) {
    if (!out || len == 0) {
        return false;
    }
    const IPAddress addr = g_wifiState == CONFIG_PORTAL ? WiFi.softAPIP() : WiFi.localIP();
    strlcpy(out, addr.toString().c_str(), len);
    return true;
}

int32_t Esp32BaseWiFi::rssi() {
    return WiFi.RSSI();
}

void Esp32BaseWiFi::setPowerSave(bool enabled) {
    g_wifiPowerSave = enabled;
    WiFi.setSleep(enabled);
}

bool Esp32BaseWiFi::powerSave() {
    return g_wifiPowerSave;
}

#endif
