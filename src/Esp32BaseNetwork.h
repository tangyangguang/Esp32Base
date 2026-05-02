#ifndef ESP32_BASE_NETWORK_H
#define ESP32_BASE_NETWORK_H

#include <stddef.h>
#include <stdint.h>

#ifndef ESP32BASE_WIFI_SSID_LEN
#define ESP32BASE_WIFI_SSID_LEN 33
#endif

#ifndef ESP32BASE_WIFI_PASS_LEN
#define ESP32BASE_WIFI_PASS_LEN 65
#endif

#ifndef ESP32BASE_WIFI_IP_LEN
#define ESP32BASE_WIFI_IP_LEN 16
#endif

#ifndef ESP32BASE_WIFI_CONNECT_TIMEOUT_MS
#define ESP32BASE_WIFI_CONNECT_TIMEOUT_MS 15000UL
#endif

#ifndef ESP32BASE_WIFI_RETRY_MIN_MS
#define ESP32BASE_WIFI_RETRY_MIN_MS 15000UL
#endif

#ifndef ESP32BASE_WIFI_RETRY_MAX_MS
#define ESP32BASE_WIFI_RETRY_MAX_MS 300000UL
#endif

#ifndef ESP32BASE_CONFIG_AP_SSID
#define ESP32BASE_CONFIG_AP_SSID "Esp32Base-Config"
#endif

using Esp32BaseNetworkCallback = void (*)();

class Esp32BaseNetwork {
public:
    enum WiFiState : uint8_t {
        WIFI_IDLE = 0,
        WIFI_CONNECTING,
        WIFI_CONNECTED,
        WIFI_CONFIG_PORTAL,
        WIFI_FAILED
    };

    static bool begin();
    static void handle();
    static bool isReady();

    static bool connect(const char* ssid, const char* password);
    static bool clearWiFi();
    static bool startConfigPortal();

    static bool isWiFiConnected();
    static WiFiState wifiState();
    static const char* wifiStateName();
    static const char* ssid();
    static const char* ip();
    static int32_t rssi();

    static void setPowerSave(bool enabled);

    static void setNtpServers(const char* s1, const char* s2 = nullptr, const char* s3 = nullptr);
    static bool isTimeSynced();
    static uint32_t timestamp();
    static bool formatTime(char* out, size_t len, const char* fmt);

    static bool isMDNSRunning();

    static void onWiFiConnected(Esp32BaseNetworkCallback cb);
    static void onWiFiDisconnected(Esp32BaseNetworkCallback cb);

    static void logConfig();

private:
    static bool loadStoredCredentials(char* ssid, size_t ssidLen, char* password, size_t passwordLen);
    static bool startConnection(const char* ssid, const char* password, bool persist);
    static void handleConnecting();
    static void handleConnected();
    static void handleDisconnected();
    static void startNtpIfNeeded();
    static void startMDNSIfNeeded();
    static uint32_t retryDelayMs();
    static void copyText(const char* in, char* out, size_t len);
    static const char* stateName(WiFiState state);

    static bool _ready;
    static WiFiState _state;
    static bool _powerSave;
    static bool _ntpStarted;
    static bool _timeSynced;
    static bool _mdnsRunning;
    static bool _lastConnected;
    static uint32_t _connectStartedMs;
    static uint32_t _nextRetryAtMs;
    static uint32_t _retryCount;
    static char _activeSsid[ESP32BASE_WIFI_SSID_LEN];
    static char _activePassword[ESP32BASE_WIFI_PASS_LEN];
    static char _ip[ESP32BASE_WIFI_IP_LEN];
    static char _ntp1[48];
    static char _ntp2[48];
    static char _ntp3[48];
    static Esp32BaseNetworkCallback _connectedCb;
    static Esp32BaseNetworkCallback _disconnectedCb;
};

#endif
