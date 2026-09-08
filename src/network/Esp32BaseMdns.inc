#include "../Esp32BaseProfile.h"

#if ESP32BASE_ENABLE_MDNS

#include "Esp32BaseMdns.h"
#include "../Esp32Base.h"
#include "../core/Esp32BaseLog.h"

#include <ESPmDNS.h>

namespace {
bool g_mdnsRunning = false;
bool g_mdnsAdvertising = false;
bool g_mdnsRetryPending = false;
uint32_t g_mdnsRetryStartedMs = 0;
constexpr uint32_t kMdnsRetryIntervalMs = 5000;
}

bool Esp32BaseMdns::begin() {
    if (g_mdnsRunning) {
        g_mdnsAdvertising = true;
        return true;
    }
    if (g_mdnsRetryPending && millis() - g_mdnsRetryStartedMs < kMdnsRetryIntervalMs) {
        return false;
    }
    g_mdnsRunning = MDNS.begin(Esp32Base::hostname());
    g_mdnsAdvertising = g_mdnsRunning;
    g_mdnsRetryPending = !g_mdnsRunning;
    if (g_mdnsRetryPending) g_mdnsRetryStartedMs = millis();
    if (g_mdnsRunning) {
        ESP32BASE_LOG_I("mdns", "mdns_started host=%s.local", Esp32Base::hostname());
    } else {
        ESP32BASE_LOG_E("mdns", "mdns_begin_failed host=%s", Esp32Base::hostname());
    }
    return g_mdnsRunning;
}

void Esp32BaseMdns::stop() {
    g_mdnsRetryPending = false;
    g_mdnsAdvertising = false;
    MDNS.end();
    g_mdnsRunning = false;
    ESP32BASE_LOG_I("mdns", "mdns_stopped");
}

bool Esp32BaseMdns::isRunning() {
    return g_mdnsRunning && g_mdnsAdvertising;
}

bool Esp32BaseMdns::addHttpService(uint16_t port) {
    if (!g_mdnsRunning) {
        return false;
    }
    MDNS.addService("http", "tcp", port);
    ESP32BASE_LOG_I("mdns", "mdns_service_added service=http tcp_port=%u", static_cast<unsigned>(port));
    return true;
}

#endif
