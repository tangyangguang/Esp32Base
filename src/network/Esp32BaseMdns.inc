#include "../Esp32BaseProfile.h"

#if ESP32BASE_ENABLE_MDNS

#include "Esp32BaseMdns.h"
#include "../Esp32Base.h"
#include "../core/Esp32BaseLog.h"

#define ESP32BASE_INCLUDE_ESPMDNS <ESPmDNS.h>
#include ESP32BASE_INCLUDE_ESPMDNS
#undef ESP32BASE_INCLUDE_ESPMDNS

namespace {
bool g_mdnsRunning = false;
bool g_mdnsAdvertising = false;
}

bool Esp32BaseMdns::begin() {
    if (g_mdnsRunning) {
        g_mdnsAdvertising = true;
        return true;
    }
    g_mdnsRunning = MDNS.begin(Esp32Base::hostname());
    g_mdnsAdvertising = g_mdnsRunning;
    if (g_mdnsRunning) {
        ESP32BASE_LOG_I("mdns", "mdns_started host=%s.local", Esp32Base::hostname());
    } else {
        ESP32BASE_LOG_E("mdns", "mdns_begin_failed host=%s", Esp32Base::hostname());
    }
    return g_mdnsRunning;
}

void Esp32BaseMdns::stop() {
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
