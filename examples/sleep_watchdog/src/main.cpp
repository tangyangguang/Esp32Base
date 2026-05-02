#include <Arduino.h>

#include <Esp32Base.h>

namespace {

constexpr uint32_t kLogIntervalMs = 5000U;
uint32_t g_lastLogMs = 0;

void logStatus() {
    uint32_t now = millis();
    if (now - g_lastLogMs < kLogIntervalMs) {
        return;
    }

    g_lastLogMs = now;
    ESP32BASE_LOG_I("SleepDemo", "reset=%s wake=%s heap=%lu wdt=%u count=%lu",
                    Esp32BaseRuntime::resetReason(),
                    Esp32BaseRuntime::wakeReason(),
                    static_cast<unsigned long>(Esp32BaseRuntime::freeHeap()),
                    Esp32BaseRuntime::isWatchdogEnabled() ? 1U : 0U,
                    static_cast<unsigned long>(Esp32BaseRuntime::watchdogCount()));
}

}  // namespace

void setup() {
    Esp32Base::setFirmwareInfo("sleep_watchdog", "0.1.0", __DATE__ " " __TIME__);
    Esp32Base::setHostname("esp32base-sleep");

    if (!Esp32Base::begin()) {
        Serial.printf("Esp32Base begin failed: %s\r\n", Esp32Base::lastError());
        return;
    }

    Esp32BaseRuntime::enableWatchdog(10000);

    // 手动测试 deep sleep 时可取消下一行注释。
    // Esp32BaseRuntime::deepSleepSeconds(10);
}

void loop() {
    Esp32Base::handle();
    logStatus();
}
