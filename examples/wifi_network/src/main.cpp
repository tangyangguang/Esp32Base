#include <Arduino.h>

#include <Esp32Base.h>

#include <string.h>

namespace {

const char* kDefaultSsid = "YOUR_WIFI_SSID";
const char* kDefaultPassword = "YOUR_WIFI_PASSWORD";
constexpr uint32_t kLogIntervalMs = 5000U;
uint32_t g_lastLogMs = 0;

void onConnected() {
    ESP32BASE_LOG_I("WifiDemo", "connected ip=%s mdns=%u",
                    Esp32BaseNetwork::ip(),
                    Esp32BaseNetwork::isMDNSRunning() ? 1U : 0U);
}

void onDisconnected() {
    ESP32BASE_LOG_W("WifiDemo", "disconnected");
}

void logNetwork() {
    uint32_t now = millis();
    if (now - g_lastLogMs < kLogIntervalMs) {
        return;
    }

    g_lastLogMs = now;

    char timeText[32] = "";
    Esp32BaseNetwork::formatTime(timeText, sizeof(timeText), "%Y-%m-%d %H:%M:%S");

    ESP32BASE_LOG_I("WifiDemo", "state=%s ssid=%s ip=%s rssi=%ld ntp=%u time=%s mdns=%u",
                    Esp32BaseNetwork::wifiStateName(),
                    Esp32BaseNetwork::ssid(),
                    Esp32BaseNetwork::ip(),
                    static_cast<long>(Esp32BaseNetwork::rssi()),
                    Esp32BaseNetwork::isTimeSynced() ? 1U : 0U,
                    timeText,
                    Esp32BaseNetwork::isMDNSRunning() ? 1U : 0U);
}

}  // namespace

void setup() {
    Esp32Base::setFirmwareInfo("wifi_network", "0.1.0", __DATE__ " " __TIME__);
    Esp32Base::setHostname("esp32base-network");
    Esp32BaseNetwork::onWiFiConnected(onConnected);
    Esp32BaseNetwork::onWiFiDisconnected(onDisconnected);
    Esp32BaseNetwork::setNtpServers("pool.ntp.org", "time.nist.gov");

    if (!Esp32Base::begin()) {
        Serial.printf("Esp32Base begin failed: %s\r\n", Esp32Base::lastError());
        return;
    }

    if (strcmp(kDefaultSsid, "YOUR_WIFI_SSID") != 0) {
        Esp32BaseNetwork::connect(kDefaultSsid, kDefaultPassword);
    }
}

void loop() {
    Esp32Base::handle();
    logNetwork();
}
