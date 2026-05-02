#include <Arduino.h>

#include <Esp32Base.h>

namespace {

constexpr uint32_t kLogIntervalMs = 5000U;
uint32_t g_lastLogMs = 0;
uint32_t g_writeCount = 0;

void updateRuntimeFile() {
    char payload[160];
    snprintf(payload, sizeof(payload),
             "{\"writes\":%lu,\"heap\":%lu,\"wdt_count\":%lu}",
             static_cast<unsigned long>(g_writeCount),
             static_cast<unsigned long>(Esp32BaseRuntime::freeHeap()),
             static_cast<unsigned long>(Esp32BaseRuntime::watchdogCount()));

    if (Esp32BaseRuntime::writeFile("/runtime.json", payload)) {
        ++g_writeCount;
    }
}

void logRuntime() {
    uint32_t now = millis();
    if (now - g_lastLogMs < kLogIntervalMs) {
        return;
    }

    g_lastLogMs = now;
    updateRuntimeFile();

    char fileText[192] = "";
    Esp32BaseRuntime::readFile("/runtime.json", fileText, sizeof(fileText));

    ESP32BASE_LOG_I("RunDemo", "heap=%lu fs=%u/%u wdt=%u count=%lu file=%s",
                    static_cast<unsigned long>(Esp32BaseRuntime::freeHeap()),
                    static_cast<unsigned>(Esp32BaseRuntime::fsUsedBytes()),
                    static_cast<unsigned>(Esp32BaseRuntime::fsTotalBytes()),
                    Esp32BaseRuntime::isWatchdogEnabled() ? 1U : 0U,
                    static_cast<unsigned long>(Esp32BaseRuntime::watchdogCount()),
                    fileText);
}

}  // namespace

void setup() {
    Esp32Base::setFirmwareInfo("runtime_tools", "0.1.0", __DATE__ " " __TIME__);
    Esp32Base::setHostname("esp32base-runtime");

    if (!Esp32Base::begin()) {
        Serial.printf("Esp32Base begin failed: %s\r\n", Esp32Base::lastError());
        return;
    }

    Esp32BaseRuntime::enableWatchdog(10000);
    updateRuntimeFile();

    // 手动测试 deep sleep 时可取消下一行注释。
    // Esp32BaseRuntime::deepSleepSeconds(10);
}

void loop() {
    Esp32Base::handle();
    logRuntime();
}
