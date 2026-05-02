#include <Arduino.h>

#include <Esp32Base.h>

namespace {

constexpr uint32_t kLogIntervalMs = 5000U;
uint32_t g_lastLogMs = 0;
uint32_t g_bootCount = 0;

void updateBootCount() {
    g_bootCount = static_cast<uint32_t>(Esp32BaseConfig::getInt("app", "boots", 0)) + 1U;
    Esp32BaseConfig::setIntDeferred("app", "boots", static_cast<int32_t>(g_bootCount));
}

void logHeartbeat() {
    uint32_t now = millis();
    if (now - g_lastLogMs < kLogIntervalMs) {
        return;
    }

    g_lastLogMs = now;
    ESP32BASE_LOG_I("Basic", "boot=%lu heap=%lu reset=%s wake=%s",
                    static_cast<unsigned long>(g_bootCount),
                    static_cast<unsigned long>(Esp32BaseRuntime::freeHeap()),
                    Esp32BaseRuntime::resetReason(),
                    Esp32BaseRuntime::wakeReason());
}

}  // namespace

void setup() {
    Esp32Base::setFirmwareInfo("basic", "0.1.0", __DATE__ " " __TIME__);
    Esp32Base::setHostname("esp32base-basic");

    if (!Esp32Base::begin()) {
        Serial.printf("Esp32Base begin failed: %s\r\n", Esp32Base::lastError());
        return;
    }

    updateBootCount();
}

void loop() {
    Esp32Base::handle();
    logHeartbeat();
}
