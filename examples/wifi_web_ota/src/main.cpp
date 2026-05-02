#include <Arduino.h>

#include <Esp32Base.h>

#include <string.h>

namespace {

const char* kDefaultSsid = "YOUR_WIFI_SSID";
const char* kDefaultPassword = "YOUR_WIFI_PASSWORD";
constexpr uint32_t kLogIntervalMs = 5000U;
uint32_t g_lastLogMs = 0;
uint32_t g_pingCount = 0;

void handleHome() {
    Esp32BaseWeb::sendHtml(
        200,
        "<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>Esp32Base App</title></head><body><h1>Esp32Base App</h1><ul>"
        "<li><a href='/api/ping'>Application ping API</a></li>"
        "<li><a href='/esp32base'>Esp32Base</a></li>"
        "<li><a href='/esp32base/wifi'>WiFi</a></li>"
        "<li><a href='/esp32base/ota'>OTA</a></li>"
        "</ul></body></html>");
}

void handlePing() {
    ++g_pingCount;
    char json[160];
    snprintf(json, sizeof(json),
             "{\"ok\":true,\"count\":%lu,\"heap\":%lu,\"ip\":\"%s\"}",
             static_cast<unsigned long>(g_pingCount),
             static_cast<unsigned long>(Esp32BaseRuntime::freeHeap()),
             Esp32BaseNetwork::ip());
    Esp32BaseWeb::sendJson(200, json);
}

void logStatus() {
    uint32_t now = millis();
    if (now - g_lastLogMs < kLogIntervalMs) {
        return;
    }

    g_lastLogMs = now;
    ESP32BASE_LOG_I("WebDemo", "state=%s ip=%s heap=%lu routes=%u/%u",
                    Esp32BaseNetwork::wifiStateName(),
                    Esp32BaseNetwork::ip(),
                    static_cast<unsigned long>(Esp32BaseRuntime::freeHeap()),
                    static_cast<unsigned>(Esp32BaseWeb::routeCount()),
                    static_cast<unsigned>(Esp32BaseWeb::routeCapacity()));
}

}  // namespace

void setup() {
    Esp32Base::setFirmwareInfo("wifi_web_ota", "0.1.0", __DATE__ " " __TIME__);
    Esp32Base::setHostname("esp32base-web");
    Esp32BaseWeb::setAuth("admin", "admin123");
    Esp32BaseWeb::addPage("/", handleHome);
    Esp32BaseWeb::addApi("/api/ping", handlePing);

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
    logStatus();
}
