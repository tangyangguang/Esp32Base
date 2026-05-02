#include "Esp32BaseNetwork.h"

#if !defined(ESP32)
#error "Esp32Base supports ESP32 Arduino Core targets only."
#endif

#include "Esp32Base.h"
#include "Esp32BaseConfig.h"
#include "Esp32BaseLog.h"

#include <ESPmDNS.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <string.h>
#include <time.h>

bool Esp32BaseNetwork::_ready = false;
Esp32BaseNetwork::WiFiState Esp32BaseNetwork::_state = Esp32BaseNetwork::WIFI_IDLE;
bool Esp32BaseNetwork::_powerSave = false;
bool Esp32BaseNetwork::_ntpStarted = false;
bool Esp32BaseNetwork::_timeSynced = false;
bool Esp32BaseNetwork::_mdnsRunning = false;
bool Esp32BaseNetwork::_lastConnected = false;
uint32_t Esp32BaseNetwork::_connectStartedMs = 0;
uint32_t Esp32BaseNetwork::_nextRetryAtMs = 0;
uint32_t Esp32BaseNetwork::_retryCount = 0;
char Esp32BaseNetwork::_activeSsid[ESP32BASE_WIFI_SSID_LEN] = "";
char Esp32BaseNetwork::_activePassword[ESP32BASE_WIFI_PASS_LEN] = "";
char Esp32BaseNetwork::_ip[ESP32BASE_WIFI_IP_LEN] = "";
char Esp32BaseNetwork::_ntp1[48] = "pool.ntp.org";
char Esp32BaseNetwork::_ntp2[48] = "time.nist.gov";
char Esp32BaseNetwork::_ntp3[48] = "";
Esp32BaseNetworkCallback Esp32BaseNetwork::_connectedCb = nullptr;
Esp32BaseNetworkCallback Esp32BaseNetwork::_disconnectedCb = nullptr;

namespace {

constexpr const char* kSsidKey = "wifi_ssid";
constexpr const char* kPassKey = "wifi_pass";

}  // namespace

bool Esp32BaseNetwork::begin() {
    if (_ready) {
        return true;
    }

    _ready = true;
    WiFi.persistent(false);
    WiFi.setAutoReconnect(false);
    setPowerSave(_powerSave);

    char storedSsid[ESP32BASE_WIFI_SSID_LEN] = "";
    char storedPass[ESP32BASE_WIFI_PASS_LEN] = "";
    if (loadStoredCredentials(storedSsid, sizeof(storedSsid), storedPass, sizeof(storedPass))) {
        startConnection(storedSsid, storedPass, false);
    } else {
        startConfigPortal();
    }

    logConfig();
    return true;
}

void Esp32BaseNetwork::handle() {
    if (!_ready) {
        return;
    }

    if (WiFi.status() == WL_CONNECTED) {
        handleConnected();
    } else if (_state == WIFI_CONNECTING) {
        handleConnecting();
    } else if (_state == WIFI_CONNECTED) {
        handleDisconnected();
    } else if (_state == WIFI_FAILED && _activeSsid[0] != '\0' && millis() >= _nextRetryAtMs) {
        startConnection(_activeSsid, _activePassword, false);
    }

    startNtpIfNeeded();
    startMDNSIfNeeded();
}

bool Esp32BaseNetwork::isReady() {
    return _ready;
}

bool Esp32BaseNetwork::connect(const char* ssid, const char* password) {
    return startConnection(ssid, password, true);
}

bool Esp32BaseNetwork::clearWiFi() {
    Esp32BaseConfig::setStr(Esp32BaseConfig::RESERVED_NAMESPACE, kSsidKey, "");
    Esp32BaseConfig::setStr(Esp32BaseConfig::RESERVED_NAMESPACE, kPassKey, "");
    _activeSsid[0] = '\0';
    _activePassword[0] = '\0';
    WiFi.disconnect(true, false);
    _ntpStarted = false;
    _timeSynced = false;
    _mdnsRunning = false;
    MDNS.end();
    return startConfigPortal();
}

bool Esp32BaseNetwork::startConfigPortal() {
    WiFi.mode(WIFI_AP_STA);
    bool ok = WiFi.softAP(ESP32BASE_CONFIG_AP_SSID);
    _state = ok ? WIFI_CONFIG_PORTAL : WIFI_FAILED;
    copyText("", _ip, sizeof(_ip));

    if (ok) {
        IPAddress apIp = WiFi.softAPIP();
        copyText(apIp.toString().c_str(), _ip, sizeof(_ip));
        ESP32BASE_LOG_I("BaseNet", "config_ap ssid=%s ip=%s", ESP32BASE_CONFIG_AP_SSID, _ip);
    } else {
        ESP32BASE_LOG_E("BaseNet", "config_ap failed");
    }

    return ok;
}

bool Esp32BaseNetwork::isWiFiConnected() {
    return WiFi.status() == WL_CONNECTED;
}

Esp32BaseNetwork::WiFiState Esp32BaseNetwork::wifiState() {
    return _state;
}

const char* Esp32BaseNetwork::wifiStateName() {
    return stateName(_state);
}

const char* Esp32BaseNetwork::ssid() {
    return _activeSsid;
}

const char* Esp32BaseNetwork::ip() {
    if (WiFi.status() == WL_CONNECTED) {
        copyText(WiFi.localIP().toString().c_str(), _ip, sizeof(_ip));
    }
    return _ip;
}

int32_t Esp32BaseNetwork::rssi() {
    if (WiFi.status() != WL_CONNECTED) {
        return 0;
    }

    return WiFi.RSSI();
}

void Esp32BaseNetwork::setPowerSave(bool enabled) {
    _powerSave = enabled;
    esp_wifi_set_ps(enabled ? WIFI_PS_MIN_MODEM : WIFI_PS_NONE);
}

void Esp32BaseNetwork::setNtpServers(const char* s1, const char* s2, const char* s3) {
    copyText(s1 == nullptr || s1[0] == '\0' ? "pool.ntp.org" : s1, _ntp1, sizeof(_ntp1));
    copyText(s2 == nullptr ? "" : s2, _ntp2, sizeof(_ntp2));
    copyText(s3 == nullptr ? "" : s3, _ntp3, sizeof(_ntp3));
    _ntpStarted = false;
    _timeSynced = false;
}

bool Esp32BaseNetwork::isTimeSynced() {
    if (!_timeSynced) {
        time_t now = time(nullptr);
        _timeSynced = now > 1609459200;
    }
    return _timeSynced;
}

uint32_t Esp32BaseNetwork::timestamp() {
    return static_cast<uint32_t>(time(nullptr));
}

bool Esp32BaseNetwork::formatTime(char* out, size_t len, const char* fmt) {
    if (out == nullptr || len == 0U) {
        return false;
    }

    out[0] = '\0';
    if (!isTimeSynced()) {
        return false;
    }

    time_t now = time(nullptr);
    struct tm localTime;
    if (!localtime_r(&now, &localTime)) {
        return false;
    }

    return strftime(out, len, fmt == nullptr ? "%Y-%m-%d %H:%M:%S" : fmt, &localTime) > 0;
}

bool Esp32BaseNetwork::isMDNSRunning() {
    return _mdnsRunning;
}

void Esp32BaseNetwork::onWiFiConnected(Esp32BaseNetworkCallback cb) {
    _connectedCb = cb;
}

void Esp32BaseNetwork::onWiFiDisconnected(Esp32BaseNetworkCallback cb) {
    _disconnectedCb = cb;
}

void Esp32BaseNetwork::logConfig() {
    ESP32BASE_LOG_I("BaseNet", "ready=%u state=%s ssid=%s ip=%s rssi=%ld ntp=%u mdns=%u ps=%u",
                    _ready ? 1U : 0U,
                    stateName(_state),
                    _activeSsid,
                    ip(),
                    static_cast<long>(rssi()),
                    isTimeSynced() ? 1U : 0U,
                    _mdnsRunning ? 1U : 0U,
                    _powerSave ? 1U : 0U);
}

bool Esp32BaseNetwork::loadStoredCredentials(char* ssid, size_t ssidLen, char* password, size_t passwordLen) {
    if (!Esp32BaseConfig::getStr(Esp32BaseConfig::RESERVED_NAMESPACE, kSsidKey, ssid, ssidLen, "")) {
        return false;
    }

    Esp32BaseConfig::getStr(Esp32BaseConfig::RESERVED_NAMESPACE, kPassKey, password, passwordLen, "");
    return ssid != nullptr && ssid[0] != '\0';
}

bool Esp32BaseNetwork::startConnection(const char* ssid, const char* password, bool persist) {
    if (ssid == nullptr || ssid[0] == '\0') {
        return false;
    }

    copyText(ssid, _activeSsid, sizeof(_activeSsid));
    copyText(password == nullptr ? "" : password, _activePassword, sizeof(_activePassword));

    if (persist) {
        Esp32BaseConfig::setStr(Esp32BaseConfig::RESERVED_NAMESPACE, kSsidKey, _activeSsid);
        Esp32BaseConfig::setStr(Esp32BaseConfig::RESERVED_NAMESPACE, kPassKey, _activePassword);
    }

    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);

    WiFi.begin(_activeSsid, _activePassword);
    _state = WIFI_CONNECTING;
    _connectStartedMs = millis();
    _nextRetryAtMs = 0;
    _ntpStarted = false;
    _timeSynced = false;
    _mdnsRunning = false;
    MDNS.end();

    ESP32BASE_LOG_I("BaseNet", "wifi connecting ssid=%s", _activeSsid);
    return true;
}

void Esp32BaseNetwork::handleConnecting() {
    uint32_t now = millis();
    if (WiFi.status() == WL_CONNECTED) {
        handleConnected();
        return;
    }

    if (now - _connectStartedMs < ESP32BASE_WIFI_CONNECT_TIMEOUT_MS) {
        return;
    }

    ++_retryCount;
    _state = WIFI_FAILED;
    _nextRetryAtMs = now + retryDelayMs();
    ESP32BASE_LOG_W("BaseNet", "wifi failed ssid=%s retry=%lu delay=%lu",
                    _activeSsid,
                    static_cast<unsigned long>(_retryCount),
                    static_cast<unsigned long>(retryDelayMs()));
}

void Esp32BaseNetwork::handleConnected() {
    _state = WIFI_CONNECTED;
    copyText(WiFi.SSID().c_str(), _activeSsid, sizeof(_activeSsid));
    copyText(WiFi.localIP().toString().c_str(), _ip, sizeof(_ip));

    if (!_lastConnected) {
        _lastConnected = true;
        _retryCount = 0;
        ESP32BASE_LOG_I("BaseNet", "wifi connected ssid=%s ip=%s rssi=%ld",
                        _activeSsid,
                        _ip,
                        static_cast<long>(WiFi.RSSI()));
        if (_connectedCb != nullptr) {
            _connectedCb();
        }
    }
}

void Esp32BaseNetwork::handleDisconnected() {
    _lastConnected = false;
    _state = WIFI_FAILED;
    _nextRetryAtMs = millis() + retryDelayMs();
    _ntpStarted = false;
    _timeSynced = false;
    _mdnsRunning = false;
    MDNS.end();
    ESP32BASE_LOG_W("BaseNet", "wifi disconnected");
    if (_disconnectedCb != nullptr) {
        _disconnectedCb();
    }
}

void Esp32BaseNetwork::startNtpIfNeeded() {
    if (_ntpStarted || WiFi.status() != WL_CONNECTED) {
        return;
    }

    configTime(0, 0, _ntp1, _ntp2[0] == '\0' ? nullptr : _ntp2, _ntp3[0] == '\0' ? nullptr : _ntp3);
    _ntpStarted = true;
    ESP32BASE_LOG_I("BaseNet", "ntp start %s %s %s", _ntp1, _ntp2, _ntp3);
}

void Esp32BaseNetwork::startMDNSIfNeeded() {
    if (_mdnsRunning || WiFi.status() != WL_CONNECTED) {
        return;
    }

    if (MDNS.begin(Esp32Base::hostname())) {
        _mdnsRunning = true;
        ESP32BASE_LOG_I("BaseNet", "mdns %s.local", Esp32Base::hostname());
    } else {
        ESP32BASE_LOG_W("BaseNet", "mdns failed");
    }
}

uint32_t Esp32BaseNetwork::retryDelayMs() {
    uint32_t delayMs = ESP32BASE_WIFI_RETRY_MIN_MS;
    if (_retryCount > 10U) {
        delayMs = ESP32BASE_WIFI_RETRY_MAX_MS;
    } else if (_retryCount > 5U) {
        delayMs = 60000UL;
    }
    return delayMs;
}

void Esp32BaseNetwork::copyText(const char* in, char* out, size_t len) {
    if (out == nullptr || len == 0U) {
        return;
    }

    const char* text = in == nullptr ? "" : in;
    strncpy(out, text, len - 1U);
    out[len - 1U] = '\0';
}

const char* Esp32BaseNetwork::stateName(WiFiState state) {
    switch (state) {
        case WIFI_IDLE:
            return "idle";
        case WIFI_CONNECTING:
            return "connecting";
        case WIFI_CONNECTED:
            return "connected";
        case WIFI_CONFIG_PORTAL:
            return "config_portal";
        case WIFI_FAILED:
            return "failed";
        default:
            return "unknown";
    }
}
