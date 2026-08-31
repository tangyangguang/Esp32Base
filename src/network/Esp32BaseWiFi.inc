#include "../Esp32BaseProfile.h"

#if ESP32BASE_ENABLE_WIFI

#include "Esp32BaseWiFi.h"
#include "../Esp32Base.h"
#include "../core/Esp32BaseConfig.h"
#include "../core/Esp32BaseLog.h"
#include "../core/Esp32BaseResetReason.h"
#include "internal/Esp32BaseRecoveryButton.h"

#if ESP32BASE_ENABLE_BUS
#include "../runtime/Esp32BaseBus.h"
#endif

#include <WiFi.h>
#include <esp_system.h>
#include <string.h>

#ifndef ESP32BASE_WIFI_CONNECT_TIMEOUT_MS
#define ESP32BASE_WIFI_CONNECT_TIMEOUT_MS 15000
#endif
#ifndef ESP32BASE_WIFI_DHCP_TIMEOUT_MS
#define ESP32BASE_WIFI_DHCP_TIMEOUT_MS 120000
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
#ifndef ESP32BASE_WIFI_SAFE_BOOT_MAX_RESETS
#define ESP32BASE_WIFI_SAFE_BOOT_MAX_RESETS 3
#endif
#ifndef ESP32BASE_WIFI_RECOVERY_BUTTON_DEBOUNCE_MS
#define ESP32BASE_WIFI_RECOVERY_BUTTON_DEBOUNCE_MS 50UL
#endif

namespace {
bool g_wifiReady = false;
Esp32BaseWiFi::State g_wifiState = Esp32BaseWiFi::IDLE;
char g_wifiSsid[33] = "";
char g_wifiPassword[65] = "";
char g_configPortalSsid[32] = "";
uint32_t g_wifiAttemptStartMs = 0;
uint32_t g_wifiDhcpWaitStartMs = 0;
uint32_t g_wifiNextRetryMs = 0;
uint8_t g_wifiRetryCount = 0;
bool g_wifiWaitingForIp = false;
bool g_wifiPowerSave = false;
bool g_staGuardActive = false;
bool g_staCredentialsPaused = false;
uint8_t g_staGuardedResetCount = 0;
bool g_wifiInitGuardActive = false;
bool g_wifiInitPaused = false;
uint8_t g_wifiInitGuardedResetCount = 0;
Esp32BaseWiFi::RecoveryButtonConfig g_recoveryButtonConfig = {
    ESP32BASE_ENABLE_WIFI_RECOVERY_BUTTON && ESP32BASE_WIFI_RECOVERY_BUTTON_DEFAULT_ENABLED,
    static_cast<int8_t>(ESP32BASE_WIFI_RECOVERY_BUTTON_GPIO),
    static_cast<uint32_t>(ESP32BASE_WIFI_RECOVERY_BUTTON_HOLD_MS)
};
bool g_recoveryButtonTriggeredEver = false;
esp32base_internal::RecoveryButtonTracker g_recoveryButtonTracker;

constexpr const char* WIFI_NS = "eb_wifi";
constexpr const char* WIFI_KEY_SSID = "ssid";
constexpr const char* WIFI_KEY_PASS = "pass";
constexpr const char* WIFI_KEY_STA_GUARD = "sta_guard";
constexpr const char* WIFI_KEY_STA_RESET_COUNT = "sta_rst";
constexpr const char* WIFI_KEY_STA_PAUSE = "sta_pause";
constexpr const char* WIFI_KEY_INIT_GUARD = "init_guard";
constexpr const char* WIFI_KEY_INIT_RESET_COUNT = "init_rst";
constexpr const char* WIFI_KEY_INIT_PAUSE = "init_pause";
constexpr const char* WIFI_RECOVERY_NS = "eb_wifi_rcv";
constexpr const char* WIFI_RECOVERY_KEY = "button";
constexpr uint8_t WIFI_RECOVERY_CONFIG_VERSION = 1;
constexpr size_t WIFI_RECOVERY_CONFIG_BYTES = 7;

bool usableRecoveryButtonGpio(int gpio) {
    if (gpio < 0 || gpio > INT8_MAX || !digitalPinIsValid(gpio) || !digitalPinCanOutput(gpio)) {
        return false;
    }
#if defined(CONFIG_IDF_TARGET_ESP32C3)
    if (gpio >= 11 && gpio <= 17) {
        return false;
    }
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
    if (gpio >= 26 && gpio <= 32) {
        return false;
    }
#else
    if (gpio >= 6 && gpio <= 11) {
        return false;
    }
#endif
    return true;
}

bool validRecoveryButtonConfig(const Esp32BaseWiFi::RecoveryButtonConfig& config) {
    return config.holdMs >= 1000UL &&
           config.holdMs <= 60000UL &&
           usableRecoveryButtonGpio(config.gpio);
}

void encodeRecoveryButtonConfig(const Esp32BaseWiFi::RecoveryButtonConfig& config,
                                uint8_t out[WIFI_RECOVERY_CONFIG_BYTES]) {
    out[0] = WIFI_RECOVERY_CONFIG_VERSION;
    out[1] = config.enabled ? 1U : 0U;
    out[2] = static_cast<uint8_t>(config.gpio);
    out[3] = static_cast<uint8_t>(config.holdMs);
    out[4] = static_cast<uint8_t>(config.holdMs >> 8);
    out[5] = static_cast<uint8_t>(config.holdMs >> 16);
    out[6] = static_cast<uint8_t>(config.holdMs >> 24);
}

bool decodeRecoveryButtonConfig(const uint8_t in[WIFI_RECOVERY_CONFIG_BYTES],
                                Esp32BaseWiFi::RecoveryButtonConfig& config) {
    if (in[0] != WIFI_RECOVERY_CONFIG_VERSION || in[1] > 1U) {
        return false;
    }
    config.enabled = in[1] != 0;
    config.gpio = static_cast<int8_t>(in[2]);
    config.holdMs = static_cast<uint32_t>(in[3]) |
                    (static_cast<uint32_t>(in[4]) << 8) |
                    (static_cast<uint32_t>(in[5]) << 16) |
                    (static_cast<uint32_t>(in[6]) << 24);
    return validRecoveryButtonConfig(config);
}

void resetRecoveryButtonRuntime() {
    g_recoveryButtonTracker.reset(millis());
}

void applyRecoveryButtonConfig(const Esp32BaseWiFi::RecoveryButtonConfig& config) {
    if (g_recoveryButtonConfig.enabled && usableRecoveryButtonGpio(g_recoveryButtonConfig.gpio) &&
        (!config.enabled || config.gpio != g_recoveryButtonConfig.gpio)) {
        pinMode(g_recoveryButtonConfig.gpio, INPUT);
    }
    g_recoveryButtonConfig = config;
    resetRecoveryButtonRuntime();
    if (config.enabled) {
        pinMode(config.gpio, INPUT_PULLUP);
    }
}

void loadRecoveryButtonConfig() {
    Esp32BaseWiFi::RecoveryButtonConfig config = Esp32BaseWiFi::defaultRecoveryButtonConfig();
    if (!validRecoveryButtonConfig(config)) {
        ESP32BASE_LOG_E("wifi", "recovery_button_default_invalid gpio=%d hold_ms=%lu action=disable",
                        static_cast<int>(config.gpio),
                        static_cast<unsigned long>(config.holdMs));
        config.enabled = false;
        config.gpio = 0;
        config.holdMs = 10000UL;
    }
    uint8_t encoded[WIFI_RECOVERY_CONFIG_BYTES];
    if (Esp32BaseConfig::getBlob(WIFI_RECOVERY_NS, WIFI_RECOVERY_KEY, encoded, sizeof(encoded))) {
        Esp32BaseWiFi::RecoveryButtonConfig stored;
        if (decodeRecoveryButtonConfig(encoded, stored)) {
            config = stored;
        } else {
            ESP32BASE_LOG_W("wifi", "recovery_button_config_invalid source=stored action=use_default");
        }
    }
    applyRecoveryButtonConfig(config);
    ESP32BASE_LOG_I("wifi", "recovery_button_config enabled=%s gpio=%d hold_ms=%lu source=%s",
                    config.enabled ? "yes" : "no",
                    static_cast<int>(config.gpio),
                    static_cast<unsigned long>(config.holdMs),
                    config.enabled ? "active" : "disabled");
}

void handleRecoveryButton() {
    if (!g_recoveryButtonConfig.enabled) {
        return;
    }
    const uint32_t now = millis();
    const bool rawPressed = digitalRead(g_recoveryButtonConfig.gpio) == LOW;
    if (!g_recoveryButtonTracker.update(rawPressed,
                                        now,
                                        ESP32BASE_WIFI_RECOVERY_BUTTON_DEBOUNCE_MS,
                                        g_recoveryButtonConfig.holdMs)) {
        return;
    }
    g_recoveryButtonTriggeredEver = true;
    ESP32BASE_LOG_W("wifi", "recovery_button_triggered gpio=%d hold_ms=%lu action=start_config_portal credentials=preserved",
                    static_cast<int>(g_recoveryButtonConfig.gpio),
                    static_cast<unsigned long>(g_recoveryButtonConfig.holdMs));
    if (!Esp32BaseWiFi::startConfigPortal()) {
        ESP32BASE_LOG_E("wifi", "recovery_button_action_failed gpio=%d action=start_config_portal",
                        static_cast<int>(g_recoveryButtonConfig.gpio));
    }
}

bool credentialsPresent() {
    return g_wifiSsid[0] != '\0';
}

bool credentialsValid(const char* ssidValue, const char* passwordValue) {
    return ssidValue && passwordValue &&
           ssidValue[0] != '\0' &&
           strlen(ssidValue) < sizeof(g_wifiSsid) &&
           strlen(passwordValue) < sizeof(g_wifiPassword);
}

const char* wlStatusName(wl_status_t status);

bool formatIpv4(const IPAddress& address, char* out, size_t len) {
    if (!out || len == 0) {
        return false;
    }
    const int written = snprintf(out,
                                 len,
                                 "%u.%u.%u.%u",
                                 static_cast<unsigned>(address[0]),
                                 static_cast<unsigned>(address[1]),
                                 static_cast<unsigned>(address[2]),
                                 static_cast<unsigned>(address[3]));
    return written >= 0 && static_cast<size_t>(written) < len;
}

uint16_t readableMacSuffix(uint64_t mac) {
    const uint8_t penultimate = static_cast<uint8_t>((mac >> 32) & 0xff);
    const uint8_t last = static_cast<uint8_t>((mac >> 40) & 0xff);
    return (static_cast<uint16_t>(penultimate) << 8) | last;
}

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
    if (!Esp32BaseConfig::getStr(WIFI_NS, WIFI_KEY_SSID, g_wifiSsid, sizeof(g_wifiSsid), "") ||
        !Esp32BaseConfig::getStr(WIFI_NS, WIFI_KEY_PASS, g_wifiPassword, sizeof(g_wifiPassword), "")) {
        g_wifiSsid[0] = '\0';
        g_wifiPassword[0] = '\0';
        return false;
    }
    return credentialsPresent();
}

bool safeBootEnabled() {
    return ESP32BASE_WIFI_SAFE_BOOT_MAX_RESETS > 0;
}

void loadStaSafeBootState() {
    g_staGuardActive = Esp32BaseConfig::getBool(WIFI_NS, WIFI_KEY_STA_GUARD, false);
    g_staCredentialsPaused = Esp32BaseConfig::getBool(WIFI_NS, WIFI_KEY_STA_PAUSE, false);
    const int32_t storedCount = Esp32BaseConfig::getInt(WIFI_NS, WIFI_KEY_STA_RESET_COUNT, 0);
    if (storedCount < 0) {
        g_staGuardedResetCount = 0;
    } else if (storedCount > 255) {
        g_staGuardedResetCount = 255;
    } else {
        g_staGuardedResetCount = static_cast<uint8_t>(storedCount);
    }

    g_wifiInitGuardActive = Esp32BaseConfig::getBool(WIFI_NS, WIFI_KEY_INIT_GUARD, false);
    g_wifiInitPaused = Esp32BaseConfig::getBool(WIFI_NS, WIFI_KEY_INIT_PAUSE, false);
    const int32_t storedInitCount = Esp32BaseConfig::getInt(WIFI_NS, WIFI_KEY_INIT_RESET_COUNT, 0);
    if (storedInitCount < 0) {
        g_wifiInitGuardedResetCount = 0;
    } else if (storedInitCount > 255) {
        g_wifiInitGuardedResetCount = 255;
    } else {
        g_wifiInitGuardedResetCount = static_cast<uint8_t>(storedInitCount);
    }
}

bool persistBoolIfNonDefault(const char* key, bool value) {
    const bool stored = Esp32BaseConfig::getBool(WIFI_NS, key, false);
    if (!value && !stored) {
        return true;
    }
    return Esp32BaseConfig::setBool(WIFI_NS, key, value);
}

bool persistIntIfNonDefault(const char* key, uint8_t value) {
    const int32_t stored = Esp32BaseConfig::getInt(WIFI_NS, key, 0);
    if (value == 0 && stored == 0) {
        return true;
    }
    return Esp32BaseConfig::setInt(WIFI_NS, key, value);
}

bool persistStaSafeBootState() {
    bool ok = true;
    ok = persistBoolIfNonDefault(WIFI_KEY_STA_GUARD, g_staGuardActive) && ok;
    ok = persistIntIfNonDefault(WIFI_KEY_STA_RESET_COUNT, g_staGuardedResetCount) && ok;
    ok = persistBoolIfNonDefault(WIFI_KEY_STA_PAUSE, g_staCredentialsPaused) && ok;
    if (!ok) {
        ESP32BASE_LOG_W("wifi", "sta_safe_boot_state_save_failed guard=%s resets=%u paused=%s",
                        g_staGuardActive ? "yes" : "no",
                        static_cast<unsigned>(g_staGuardedResetCount),
                        g_staCredentialsPaused ? "yes" : "no");
    }
    return ok;
}

bool persistWiFiInitSafeBootState() {
    bool ok = true;
    ok = persistBoolIfNonDefault(WIFI_KEY_INIT_GUARD, g_wifiInitGuardActive) && ok;
    ok = persistIntIfNonDefault(WIFI_KEY_INIT_RESET_COUNT, g_wifiInitGuardedResetCount) && ok;
    ok = persistBoolIfNonDefault(WIFI_KEY_INIT_PAUSE, g_wifiInitPaused) && ok;
    if (!ok) {
        ESP32BASE_LOG_W("wifi", "wifi_init_safe_boot_state_save_failed guard=%s resets=%u paused=%s",
                        g_wifiInitGuardActive ? "yes" : "no",
                        static_cast<unsigned>(g_wifiInitGuardedResetCount),
                        g_wifiInitPaused ? "yes" : "no");
    }
    return ok;
}

void clearStaSafeBootState(const char* reason) {
    const bool hadState = g_staGuardActive || g_staCredentialsPaused || g_staGuardedResetCount != 0;
    const uint8_t previousCount = g_staGuardedResetCount;
    const bool previousPaused = g_staCredentialsPaused;
    g_staGuardActive = false;
    g_staGuardedResetCount = 0;
    g_staCredentialsPaused = false;
    if (hadState) {
        persistStaSafeBootState();
    }
    if (hadState) {
        ESP32BASE_LOG_I("wifi", "sta_safe_boot_cleared reason=%s previous_resets=%u previous_paused=%s",
                        reason ? reason : "unknown",
                        static_cast<unsigned>(previousCount),
                        previousPaused ? "yes" : "no");
    }
}

void clearWiFiInitSafeBootState(const char* reason) {
    const bool hadState = g_wifiInitGuardActive || g_wifiInitPaused || g_wifiInitGuardedResetCount != 0;
    const uint8_t previousCount = g_wifiInitGuardedResetCount;
    const bool previousPaused = g_wifiInitPaused;
    g_wifiInitGuardActive = false;
    g_wifiInitGuardedResetCount = 0;
    g_wifiInitPaused = false;
    if (hadState) {
        persistWiFiInitSafeBootState();
        ESP32BASE_LOG_I("wifi", "wifi_init_safe_boot_cleared reason=%s previous_resets=%u previous_paused=%s",
                        reason ? reason : "unknown",
                        static_cast<unsigned>(previousCount),
                        previousPaused ? "yes" : "no");
    }
}

void resumeStaCredentials(const char* reason) {
    const bool wasPaused = g_staCredentialsPaused;
    clearStaSafeBootState(reason);
    clearWiFiInitSafeBootState(reason);
    if (wasPaused) {
        ESP32BASE_LOG_I("wifi", "sta_safe_boot_resume reason=%s ssid=%s", reason ? reason : "unknown", g_wifiSsid);
    }
}

bool guardedResetReason(esp_reset_reason_t reason) {
    switch (reason) {
        case ESP_RST_BROWNOUT:
        case ESP_RST_PANIC:
        case ESP_RST_INT_WDT:
        case ESP_RST_TASK_WDT:
        case ESP_RST_WDT:
            return true;
        default:
            return false;
    }
}

bool wifiInitGuardedResetReason(esp_reset_reason_t reason) {
    return guardedResetReason(reason) || reason == ESP_RST_SW;
}

void logWiFiInitPowerWarning() {
    ESP32BASE_LOG_W("wifi",
                    "电源风险：疑似 WiFi/RF 启动瞬时电流导致供电跌落。请检查电源、USB线、稳压器余量和接线，并考虑在板端 VIN/5V 与 GND 间增加低 ESR 储能电容；不会关闭 brownout detector。");
}

void autoResumeStaCredentialsForSafeBoot(const char* reason, uint8_t previousCount) {
    ESP32BASE_LOG_I("wifi",
                    "sta_safe_boot_auto_resume ssid=%s reason=%s previous_resets=%u action=retry_sta",
                    g_wifiSsid,
                    reason ? reason : "unknown",
                    static_cast<unsigned>(previousCount));
    clearStaSafeBootState("normal_reset_auto_resume");
}

void pauseStaCredentialsForSafeBoot(const char* reason, uint8_t guardedResetCount) {
    g_staGuardActive = false;
    g_staCredentialsPaused = true;
    g_staGuardedResetCount = guardedResetCount;
    persistStaSafeBootState();
    ESP32BASE_LOG_W("wifi",
                    "sta_safe_boot_pause ssid=%s reason=%s guarded_resets=%u max=%u action=config_portal credentials=paused",
                    g_wifiSsid,
                    reason ? reason : "unknown",
                    static_cast<unsigned>(g_staGuardedResetCount),
                    static_cast<unsigned>(ESP32BASE_WIFI_SAFE_BOOT_MAX_RESETS));
    publish(Esp32BaseWiFi::EVENT_FAILED);
}

void pauseWiFiInitForSafeBoot(const char* reason, uint8_t guardedResetCount) {
    g_wifiInitGuardActive = false;
    g_wifiInitPaused = true;
    g_wifiInitGuardedResetCount = guardedResetCount;
    persistWiFiInitSafeBootState();
    ESP32BASE_LOG_W("wifi",
                    "wifi_init_safe_boot_pause reason=%s guarded_resets=%u max=%u action=skip_wifi",
                    reason ? reason : "unknown",
                    static_cast<unsigned>(g_wifiInitGuardedResetCount),
                    static_cast<unsigned>(ESP32BASE_WIFI_SAFE_BOOT_MAX_RESETS));
    logWiFiInitPowerWarning();
    publish(Esp32BaseWiFi::EVENT_FAILED);
}

bool evaluateWiFiInitSafeBootBeforeArduinoWiFi() {
    if (!safeBootEnabled()) {
        if (g_wifiInitGuardActive || g_wifiInitPaused || g_wifiInitGuardedResetCount != 0) {
            clearWiFiInitSafeBootState("safe_boot_disabled");
        }
        return true;
    }

    const esp_reset_reason_t reason = esp_reset_reason();
    if (g_wifiInitPaused) {
        if (!wifiInitGuardedResetReason(reason)) {
            ESP32BASE_LOG_I("wifi",
                            "wifi_init_safe_boot_auto_resume reason=%s previous_resets=%u action=retry_wifi_init",
                            esp32base_internal::resetReasonName(reason),
                            static_cast<unsigned>(g_wifiInitGuardedResetCount));
            clearWiFiInitSafeBootState("normal_reset_auto_resume");
            return true;
        }
        ESP32BASE_LOG_W("wifi",
                        "wifi_init_safe_boot_paused reset_reason=%s guarded_resets=%u action=skip_wifi",
                        esp32base_internal::resetReasonName(reason),
                        static_cast<unsigned>(g_wifiInitGuardedResetCount));
        logWiFiInitPowerWarning();
        return false;
    }

    if (!g_wifiInitGuardActive) {
        return true;
    }
    if (!wifiInitGuardedResetReason(reason)) {
        ESP32BASE_LOG_I("wifi", "wifi_init_safe_boot_previous_guard_ignored reset_reason=%s action=retry_wifi_init",
                        esp32base_internal::resetReasonName(reason));
        clearWiFiInitSafeBootState("normal_reset");
        return true;
    }
    if (g_wifiInitGuardedResetCount < 255) {
        ++g_wifiInitGuardedResetCount;
    }
    if (g_wifiInitGuardedResetCount >= ESP32BASE_WIFI_SAFE_BOOT_MAX_RESETS) {
        pauseWiFiInitForSafeBoot(esp32base_internal::resetReasonName(reason), g_wifiInitGuardedResetCount);
        return false;
    }
    persistWiFiInitSafeBootState();
    ESP32BASE_LOG_W("wifi",
                    "wifi_init_safe_boot_guarded_reset reset_reason=%s guarded_resets=%u max=%u action=retry_wifi_init",
                    esp32base_internal::resetReasonName(reason),
                    static_cast<unsigned>(g_wifiInitGuardedResetCount),
                    static_cast<unsigned>(ESP32BASE_WIFI_SAFE_BOOT_MAX_RESETS));
    return true;
}

void markWiFiInitSafeBootGuardActive() {
    if (!safeBootEnabled() || g_wifiInitGuardActive) {
        return;
    }
    g_wifiInitGuardActive = true;
    persistWiFiInitSafeBootState();
    ESP32BASE_LOG_I("wifi", "wifi_init_safe_boot_guard_set max=%u",
                    static_cast<unsigned>(ESP32BASE_WIFI_SAFE_BOOT_MAX_RESETS));
}

bool evaluateStaSafeBootBeforeSta() {
    if (!safeBootEnabled()) {
        if (g_staGuardActive || g_staCredentialsPaused || g_staGuardedResetCount != 0) {
            clearStaSafeBootState("safe_boot_disabled");
        }
        return true;
    }
    if (g_staCredentialsPaused) {
        const esp_reset_reason_t reason = esp_reset_reason();
        if (!guardedResetReason(reason)) {
            autoResumeStaCredentialsForSafeBoot(esp32base_internal::resetReasonName(reason), g_staGuardedResetCount);
            return true;
        }
        ESP32BASE_LOG_W("wifi",
                        "sta_safe_boot_paused ssid=%s reset_reason=%s guarded_resets=%u action=config_portal",
                        g_wifiSsid,
                        esp32base_internal::resetReasonName(reason),
                        static_cast<unsigned>(g_staGuardedResetCount));
        return false;
    }
    if (!g_staGuardActive) {
        return true;
    }
    const esp_reset_reason_t reason = esp_reset_reason();
    if (!guardedResetReason(reason)) {
        ESP32BASE_LOG_I("wifi", "sta_safe_boot_previous_guard_ignored reset_reason=%s action=retry_sta",
                        esp32base_internal::resetReasonName(reason));
        clearStaSafeBootState("normal_reset");
        return true;
    }
    if (g_staGuardedResetCount < 255) {
        ++g_staGuardedResetCount;
    }
    if (g_staGuardedResetCount >= ESP32BASE_WIFI_SAFE_BOOT_MAX_RESETS) {
        pauseStaCredentialsForSafeBoot(esp32base_internal::resetReasonName(reason), g_staGuardedResetCount);
        return false;
    }
    persistStaSafeBootState();
    ESP32BASE_LOG_W("wifi",
                    "sta_safe_boot_guarded_reset ssid=%s reset_reason=%s guarded_resets=%u max=%u action=retry_sta",
                    g_wifiSsid,
                    esp32base_internal::resetReasonName(reason),
                    static_cast<unsigned>(g_staGuardedResetCount),
                    static_cast<unsigned>(ESP32BASE_WIFI_SAFE_BOOT_MAX_RESETS));
    return true;
}

void markStaSafeBootGuardActive() {
    if (!safeBootEnabled() || g_staGuardActive) {
        return;
    }
    g_staGuardActive = true;
    persistStaSafeBootState();
    ESP32BASE_LOG_I("wifi", "sta_safe_boot_guard_set ssid=%s max=%u",
                    g_wifiSsid,
                    static_cast<unsigned>(ESP32BASE_WIFI_SAFE_BOOT_MAX_RESETS));
}

bool beginSta() {
    if (!credentialsPresent()) {
        g_wifiState = Esp32BaseWiFi::IDLE;
        ESP32BASE_LOG_W("wifi", "sta connect skipped: no credentials");
        return false;
    }
    markStaSafeBootGuardActive();
    if (!WiFi.mode(WIFI_STA)) {
        g_wifiState = Esp32BaseWiFi::FAILED;
        ESP32BASE_LOG_E("wifi", "station_mode_failed ssid=%s action=config_portal", g_wifiSsid);
        pauseStaCredentialsForSafeBoot("mode_failed", g_staGuardedResetCount);
        return false;
    }
    if (!WiFi.setHostname(Esp32Base::hostname())) {
        ESP32BASE_LOG_W("wifi", "hostname_apply_failed value=%s", Esp32Base::hostname());
    }
    if (g_wifiPassword[0]) {
        WiFi.begin(g_wifiSsid, g_wifiPassword);
    } else {
        WiFi.begin(g_wifiSsid);
    }
    g_wifiAttemptStartMs = millis();
    g_wifiDhcpWaitStartMs = 0;
    g_wifiWaitingForIp = false;
    g_wifiState = Esp32BaseWiFi::CONNECTING;
    ESP32BASE_LOG_I("wifi", "station_connecting ssid=%s password_set=%s status=%s status_code=%u",
                    g_wifiSsid,
                    g_wifiPassword[0] ? "yes" : "no",
                    wlStatusName(WiFi.status()),
                    static_cast<unsigned>(WiFi.status()));
    return true;
}

void scheduleRetry(uint32_t now, const char* reason) {
    g_wifiDhcpWaitStartMs = 0;
    g_wifiWaitingForIp = false;
    g_wifiState = Esp32BaseWiFi::RETRY_BACKOFF;
    const uint32_t delayMs = retryDelayMs();
    const bool slowBackoff = g_wifiRetryCount >= ESP32BASE_WIFI_RETRY_FAST_COUNT;
    g_wifiNextRetryMs = now + delayMs;
    if (slowBackoff) {
        ESP32BASE_LOG_I("wifi", "%s attempt=%u retry_in=%lu ms mode=%s status=%s status_code=%u rssi=%ld",
                        reason,
                        static_cast<unsigned>(g_wifiRetryCount + 1),
                        static_cast<unsigned long>(delayMs),
                        "slow",
                        wlStatusName(WiFi.status()),
                        static_cast<unsigned>(WiFi.status()),
                        static_cast<long>(WiFi.RSSI()));
    } else {
        ESP32BASE_LOG_W("wifi", "%s attempt=%u retry_in=%lu ms mode=%s status=%s status_code=%u rssi=%ld",
                        reason,
                        static_cast<unsigned>(g_wifiRetryCount + 1),
                        static_cast<unsigned long>(delayMs),
                        "fast",
                        wlStatusName(WiFi.status()),
                        static_cast<unsigned>(WiFi.status()),
                        static_cast<long>(WiFi.RSSI()));
    }
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
#if ESP32BASE_ENABLE_WIFI_RECOVERY_BUTTON
    loadRecoveryButtonConfig();
#endif
    const bool hasCredentials = loadCredentials();
    loadStaSafeBootState();
    if (!evaluateWiFiInitSafeBootBeforeArduinoWiFi()) {
        g_wifiState = FAILED;
        return true;
    }
    markWiFiInitSafeBootGuardActive();
    WiFi.persistent(false);
    WiFi.setSleep(g_wifiPowerSave);
    if (hasCredentials) {
        if (evaluateStaSafeBootBeforeSta()) {
            if (!beginSta()) {
                startConfigPortal();
            }
        } else {
            startConfigPortal();
        }
    } else {
        clearStaSafeBootState("no_credentials");
        startConfigPortal();
    }
    clearWiFiInitSafeBootState("wifi_begin_completed");
    return true;
}

void Esp32BaseWiFi::handle() {
    if (!g_wifiReady) {
        return;
    }
#if ESP32BASE_ENABLE_WIFI_RECOVERY_BUTTON
    handleRecoveryButton();
#endif
    const wl_status_t status = WiFi.status();
    const uint32_t now = millis();
    if ((g_wifiState == CONNECTING || g_wifiState == RETRY_BACKOFF) && status == WL_CONNECTED) {
        g_wifiDhcpWaitStartMs = 0;
        g_wifiWaitingForIp = false;
        g_wifiState = CONNECTED;
        g_wifiRetryCount = 0;
        clearStaSafeBootState("connected");
        char ipAddress[16];
        formatIpv4(WiFi.localIP(), ipAddress, sizeof(ipAddress));
        ESP32BASE_LOG_I("wifi", "connected ip=%s", ipAddress);
        publish(EVENT_CONNECTED);
        return;
    }
    if (g_wifiState == CONNECTED && status != WL_CONNECTED) {
        if (status == WL_IDLE_STATUS) {
            g_wifiState = CONNECTING;
            g_wifiAttemptStartMs = now;
            g_wifiDhcpWaitStartMs = now;
            g_wifiWaitingForIp = true;
            ESP32BASE_LOG_W("wifi", "station_ip_lost status=%s status_code=%u action=wait_for_dhcp timeout=%lu ms rssi=%ld",
                            wlStatusName(status),
                            static_cast<unsigned>(status),
                            static_cast<unsigned long>(ESP32BASE_WIFI_DHCP_TIMEOUT_MS),
                            static_cast<long>(WiFi.RSSI()));
            publish(EVENT_DISCONNECTED);
            return;
        }
        ESP32BASE_LOG_W("wifi", "station_disconnected status=%s status_code=%u rssi=%ld",
                        wlStatusName(status), static_cast<unsigned>(status), static_cast<long>(WiFi.RSSI()));
        publish(EVENT_DISCONNECTED);
        scheduleRetry(now, "connection lost");
        return;
    }
    if (g_wifiState == CONNECTING && status == WL_IDLE_STATUS) {
        if (!g_wifiWaitingForIp) {
            g_wifiDhcpWaitStartMs = now;
            g_wifiWaitingForIp = true;
            ESP32BASE_LOG_I("wifi", "station_associated_waiting_for_ip ssid=%s timeout=%lu ms rssi=%ld",
                            g_wifiSsid,
                            static_cast<unsigned long>(ESP32BASE_WIFI_DHCP_TIMEOUT_MS),
                            static_cast<long>(WiFi.RSSI()));
        }
        if (static_cast<int32_t>(now - g_wifiDhcpWaitStartMs) >= ESP32BASE_WIFI_DHCP_TIMEOUT_MS) {
            ESP32BASE_LOG_W("wifi", "station_dhcp_timeout ssid=%s elapsed=%lu ms rssi=%ld",
                            g_wifiSsid,
                            static_cast<unsigned long>(now - g_wifiDhcpWaitStartMs),
                            static_cast<long>(WiFi.RSSI()));
            scheduleRetry(now, "dhcp timeout");
        }
        return;
    }
    if (g_wifiWaitingForIp) {
        g_wifiDhcpWaitStartMs = 0;
        g_wifiWaitingForIp = false;
    }
    if (g_wifiState == CONNECTING && status != WL_CONNECTED &&
        static_cast<int32_t>(now - g_wifiAttemptStartMs) >= ESP32BASE_WIFI_CONNECT_TIMEOUT_MS) {
        const bool slowBackoff = g_wifiRetryCount >= ESP32BASE_WIFI_RETRY_FAST_COUNT;
        if (slowBackoff) {
            ESP32BASE_LOG_I("wifi", "station_connect_timeout ssid=%s status=%s status_code=%u elapsed=%lu ms rssi=%ld retry_phase=slow",
                            g_wifiSsid,
                            wlStatusName(status),
                            static_cast<unsigned>(status),
                            static_cast<unsigned long>(now - g_wifiAttemptStartMs),
                            static_cast<long>(WiFi.RSSI()));
        } else {
            ESP32BASE_LOG_W("wifi", "station_connect_timeout ssid=%s status=%s status_code=%u elapsed=%lu ms rssi=%ld retry_phase=fast",
                            g_wifiSsid,
                            wlStatusName(status),
                            static_cast<unsigned>(status),
                            static_cast<unsigned long>(now - g_wifiAttemptStartMs),
                            static_cast<long>(WiFi.RSSI()));
        }
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
        if (!beginSta()) {
            startConfigPortal();
        }
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
        if (!Esp32BaseConfig::setStr(WIFI_NS, WIFI_KEY_PASS, passwordValue) ||
            !Esp32BaseConfig::setStr(WIFI_NS, WIFI_KEY_SSID, ssidValue)) {
            ESP32BASE_LOG_E("wifi", "credential save failed");
            return false;
        }
        char verifySsid[33] = "";
        char verifyPassword[65] = "";
        Esp32BaseConfig::getStr(WIFI_NS, WIFI_KEY_SSID, verifySsid, sizeof(verifySsid), "");
        Esp32BaseConfig::getStr(WIFI_NS, WIFI_KEY_PASS, verifyPassword, sizeof(verifyPassword), "");
        if (strcmp(verifySsid, ssidValue) != 0 || strcmp(verifyPassword, passwordValue) != 0) {
            ESP32BASE_LOG_E("wifi", "credential save verify failed");
            return false;
        }
    }
    strlcpy(g_wifiSsid, ssidValue, sizeof(g_wifiSsid));
    strlcpy(g_wifiPassword, passwordValue, sizeof(g_wifiPassword));
    ESP32BASE_LOG_I("wifi", "credentials_set ssid=%s password_set=%s persist=%s",
                    g_wifiSsid,
                    g_wifiPassword[0] ? "yes" : "no",
                    persist ? "yes" : "no");
    g_wifiRetryCount = 0;
    resumeStaCredentials("credentials_set");
    stopConfigPortal();
    if (beginSta()) {
        return true;
    }
    startConfigPortal();
    return false;
}

bool Esp32BaseWiFi::clearCredentials() {
    const bool hadSafeBootState = g_staGuardActive || g_staCredentialsPaused || g_staGuardedResetCount != 0;
    const uint8_t previousCount = g_staGuardedResetCount;
    const bool previousPaused = g_staCredentialsPaused;
    g_wifiSsid[0] = '\0';
    g_wifiPassword[0] = '\0';
    g_staGuardActive = false;
    g_staCredentialsPaused = false;
    g_staGuardedResetCount = 0;
    g_wifiInitGuardActive = false;
    g_wifiInitPaused = false;
    g_wifiInitGuardedResetCount = 0;
    if (hadSafeBootState) {
        ESP32BASE_LOG_I("wifi", "sta_safe_boot_cleared reason=credentials_cleared previous_resets=%u previous_paused=%s",
                        static_cast<unsigned>(previousCount),
                        previousPaused ? "yes" : "no");
    }
    const bool ok = Esp32BaseConfig::clearNamespace(WIFI_NS);
    if (!ok) {
        ESP32BASE_LOG_W("wifi", "credential_clear_failed");
    }
    return ok;
}

bool Esp32BaseWiFi::retrySavedCredentials() {
    if (!loadCredentials()) {
        ESP32BASE_LOG_W("wifi", "saved_credentials_retry_failed reason=no_credentials");
        return false;
    }
    g_wifiRetryCount = 0;
    resumeStaCredentials("saved_credentials_retry");
    stopConfigPortal();
    if (beginSta()) {
        ESP32BASE_LOG_I("wifi", "saved_credentials_retry_started ssid=%s", g_wifiSsid);
        return true;
    }
    startConfigPortal();
    return false;
}

bool Esp32BaseWiFi::startConfigPortal() {
    WiFi.mode(WIFI_AP);
    g_wifiRetryCount = 0;
    if (!g_configPortalSsid[0]) {
        snprintf(g_configPortalSsid, sizeof(g_configPortalSsid), "ESP32-Config-%04X",
                 static_cast<unsigned>(readableMacSuffix(ESP.getEfuseMac())));
    }
    const bool ok = WiFi.softAP(g_configPortalSsid);
    g_wifiState = ok ? CONFIG_PORTAL : FAILED;
    if (ok) {
        publish(EVENT_CONFIG_PORTAL);
        char ipAddress[16];
        formatIpv4(WiFi.softAPIP(), ipAddress, sizeof(ipAddress));
        ESP32BASE_LOG_I("wifi", "config_portal_started ssid=%s ip=%s", g_configPortalSsid, ipAddress);
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

Esp32BaseWiFi::RecoveryButtonConfig Esp32BaseWiFi::recoveryButtonConfig() {
    return g_recoveryButtonConfig;
}

Esp32BaseWiFi::RecoveryButtonConfig Esp32BaseWiFi::defaultRecoveryButtonConfig() {
    RecoveryButtonConfig config = {
        ESP32BASE_ENABLE_WIFI_RECOVERY_BUTTON && ESP32BASE_WIFI_RECOVERY_BUTTON_DEFAULT_ENABLED,
        static_cast<int8_t>(ESP32BASE_WIFI_RECOVERY_BUTTON_GPIO),
        static_cast<uint32_t>(ESP32BASE_WIFI_RECOVERY_BUTTON_HOLD_MS)
    };
    return config;
}

bool Esp32BaseWiFi::setRecoveryButtonConfig(const RecoveryButtonConfig& config) {
#if !ESP32BASE_ENABLE_WIFI_RECOVERY_BUTTON
    (void)config;
    ESP32BASE_LOG_W("wifi", "recovery_button_config_rejected reason=capability_disabled");
    return false;
#else
    if (!isValidRecoveryButtonConfig(config)) {
        ESP32BASE_LOG_W("wifi", "recovery_button_config_rejected enabled=%s gpio=%d hold_ms=%lu",
                        config.enabled ? "yes" : "no",
                        static_cast<int>(config.gpio),
                        static_cast<unsigned long>(config.holdMs));
        return false;
    }
    uint8_t encoded[WIFI_RECOVERY_CONFIG_BYTES];
    encodeRecoveryButtonConfig(config, encoded);
    if (!Esp32BaseConfig::setBlob(WIFI_RECOVERY_NS, WIFI_RECOVERY_KEY, encoded, sizeof(encoded))) {
        ESP32BASE_LOG_W("wifi", "recovery_button_config_save_failed");
        return false;
    }
    uint8_t verify[WIFI_RECOVERY_CONFIG_BYTES];
    if (!Esp32BaseConfig::getBlob(WIFI_RECOVERY_NS, WIFI_RECOVERY_KEY, verify, sizeof(verify)) ||
        memcmp(encoded, verify, sizeof(encoded)) != 0) {
        ESP32BASE_LOG_W("wifi", "recovery_button_config_verify_failed");
        return false;
    }
    applyRecoveryButtonConfig(config);
    ESP32BASE_LOG_W("wifi", "recovery_button_config_saved enabled=%s gpio=%d hold_ms=%lu",
                    config.enabled ? "yes" : "no",
                    static_cast<int>(config.gpio),
                    static_cast<unsigned long>(config.holdMs));
    return true;
#endif
}

bool Esp32BaseWiFi::isValidRecoveryButtonConfig(const RecoveryButtonConfig& config) {
    return validRecoveryButtonConfig(config);
}

bool Esp32BaseWiFi::recoveryButtonTriggered() {
    return g_recoveryButtonTriggeredEver;
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

bool Esp32BaseWiFi::safeBootPaused() {
    return g_staCredentialsPaused;
}

uint8_t Esp32BaseWiFi::safeBootGuardedResetCount() {
    return g_staGuardedResetCount;
}

uint8_t Esp32BaseWiFi::retryCount() {
    return g_wifiRetryCount;
}

uint32_t Esp32BaseWiFi::retryRemainingMs() {
    if (g_wifiState != RETRY_BACKOFF) {
        return 0;
    }
    const uint32_t now = millis();
    return static_cast<int32_t>(now - g_wifiNextRetryMs) >= 0 ? 0 : g_wifiNextRetryMs - now;
}

bool Esp32BaseWiFi::ip(char* out, size_t len) {
    if (!out || len == 0) {
        return false;
    }
    const IPAddress address = g_wifiState == CONFIG_PORTAL ? WiFi.softAPIP() : WiFi.localIP();
    return formatIpv4(address, out, len);
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
