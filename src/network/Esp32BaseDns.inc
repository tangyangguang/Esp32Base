#include "../Esp32BaseProfile.h"

#if ESP32BASE_ENABLE_DNS

#include "Esp32BaseDns.h"
#include "Esp32BaseWiFi.h"

#define ESP32BASE_INCLUDE_DNSSERVER <DNSServer.h>
#include ESP32BASE_INCLUDE_DNSSERVER
#undef ESP32BASE_INCLUDE_DNSSERVER
#define ESP32BASE_INCLUDE_WIFI <WiFi.h>
#include ESP32BASE_INCLUDE_WIFI
#undef ESP32BASE_INCLUDE_WIFI

namespace {
DNSServer g_dnsServer;
bool g_dnsServerRunning = false;
}

bool Esp32BaseDns::begin() {
    if (Esp32BaseWiFi::state() != Esp32BaseWiFi::CONFIG_PORTAL) {
        return false;
    }
    g_dnsServerRunning = g_dnsServer.start(53, "*", WiFi.softAPIP());
    return g_dnsServerRunning;
}

void Esp32BaseDns::handle() {
    if (g_dnsServerRunning) {
        g_dnsServer.processNextRequest();
    }
}

void Esp32BaseDns::stop() {
    if (g_dnsServerRunning) {
        g_dnsServer.stop();
        g_dnsServerRunning = false;
    }
}

bool Esp32BaseDns::isRunning() {
    return g_dnsServerRunning;
}

#endif
