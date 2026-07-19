#pragma once

#include <Arduino.h>
#include <stdint.h>

class Esp32BaseWiFi {
public:
    struct RecoveryButtonConfig {
        bool enabled;
        int8_t gpio;
        uint32_t holdMs;
    };

    enum State : uint8_t {
        IDLE,
        CONNECTING,
        CONNECTED,
        CONFIG_PORTAL,
        RETRY_BACKOFF,
        FAILED
    };

    static constexpr const char* EVENT_CONNECTED = "wifi.connected";
    static constexpr const char* EVENT_DISCONNECTED = "wifi.disconnected";
    static constexpr const char* EVENT_CONFIG_PORTAL = "wifi.config_portal";
    static constexpr const char* EVENT_RETRY = "wifi.retry";
    static constexpr const char* EVENT_FAILED = "wifi.failed";

    static bool begin();
    static void handle();
    static bool isReady();

    static bool connect(const char* ssid, const char* password, bool persist = true);
    static bool clearCredentials();
    static bool retrySavedCredentials();
    static bool startConfigPortal();
    static bool stopConfigPortal();
    static RecoveryButtonConfig recoveryButtonConfig();
    static RecoveryButtonConfig defaultRecoveryButtonConfig();
    static bool setRecoveryButtonConfig(const RecoveryButtonConfig& config);
    static bool isValidRecoveryButtonConfig(const RecoveryButtonConfig& config);
    static bool recoveryButtonTriggered();

    static State state();
    static const char* stateName();
    static bool isConnected();
    static const char* ssid();
    static bool safeBootPaused();
    static uint8_t safeBootGuardedResetCount();
    static bool ip(char* out, size_t len);
    static int32_t rssi();

    static void setPowerSave(bool enabled);
    static bool powerSave();
};
