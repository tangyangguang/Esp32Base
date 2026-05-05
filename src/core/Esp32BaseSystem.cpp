#include "Esp32BaseSystem.h"

#include "Esp32BaseConfig.h"
#include "Esp32BaseLog.h"
#include "Esp32BaseUtil.h"

#include "../Esp32BaseProfile.h"
#if ESP32BASE_ENABLE_FILELOG
#include "../runtime/Esp32BaseFileLog.h"
#endif

#include <esp_system.h>
#include <esp_sleep.h>

namespace {
bool g_ready = false;
uint8_t g_restartLogCount = 0;

constexpr uint8_t RESTART_LOG_CAPACITY = 4;

const char* resetReasonName(esp_reset_reason_t reason) {
    switch (reason) {
        case ESP_RST_POWERON: return "poweron";
        case ESP_RST_EXT: return "external";
        case ESP_RST_SW: return "software";
        case ESP_RST_PANIC: return "panic";
        case ESP_RST_INT_WDT: return "int_wdt";
        case ESP_RST_TASK_WDT: return "task_wdt";
        case ESP_RST_WDT: return "wdt";
        case ESP_RST_DEEPSLEEP: return "deep_sleep";
        case ESP_RST_BROWNOUT: return "brownout";
        case ESP_RST_SDIO: return "sdio";
        case ESP_RST_UNKNOWN:
        default: return "unknown";
    }
}

const char* wakeReasonName(esp_sleep_wakeup_cause_t cause) {
    switch (cause) {
        case ESP_SLEEP_WAKEUP_EXT0: return "ext0";
        case ESP_SLEEP_WAKEUP_EXT1: return "ext1";
        case ESP_SLEEP_WAKEUP_TIMER: return "timer";
        case ESP_SLEEP_WAKEUP_TOUCHPAD: return "touchpad";
        case ESP_SLEEP_WAKEUP_ULP: return "ulp";
#if defined(ESP_SLEEP_WAKEUP_GPIO)
        case ESP_SLEEP_WAKEUP_GPIO: return "gpio";
#endif
#if defined(ESP_SLEEP_WAKEUP_UART)
        case ESP_SLEEP_WAKEUP_UART: return "uart";
#endif
        case ESP_SLEEP_WAKEUP_UNDEFINED:
        default: return "undefined";
    }
}
}

bool Esp32BaseSystem::begin() {
    g_restartLogCount = static_cast<uint8_t>(Esp32BaseConfig::getInt("eb_sys", "rst_cnt", 0));
    g_ready = true;
    return true;
}

bool Esp32BaseSystem::isReady() {
    return g_ready;
}

uint32_t Esp32BaseSystem::freeHeap() {
    return ESP.getFreeHeap();
}

uint32_t Esp32BaseSystem::minFreeHeap() {
#if defined(ESP_ARDUINO_VERSION_MAJOR)
    return ESP.getMinFreeHeap();
#else
    return ESP.getFreeHeap();
#endif
}

uint32_t Esp32BaseSystem::totalHeap() {
#if defined(ESP_ARDUINO_VERSION_MAJOR)
    return ESP.getHeapSize();
#else
    return 0;
#endif
}

uint32_t Esp32BaseSystem::flashSize() {
    return ESP.getFlashChipSize();
}

uint32_t Esp32BaseSystem::uptimeMs() {
    return millis();
}

const char* Esp32BaseSystem::resetReason() {
    return resetReasonName(esp_reset_reason());
}

const char* Esp32BaseSystem::wakeReason() {
    return wakeReasonName(esp_sleep_get_wakeup_cause());
}

void Esp32BaseSystem::restart(const char* reason) {
    appendRestartLog(reason ? reason : "restart");
    Esp32BaseConfig::flushAll();
#if ESP32BASE_ENABLE_FILELOG
    Esp32BaseFileLog::flush();
#endif
    ESP32BASE_LOG_W("system", "restart: %s", reason ? reason : "");
#if ESP32BASE_ENABLE_FILELOG
    Esp32BaseFileLog::flush();
#endif
    delay(50);
    ESP.restart();
}

bool Esp32BaseSystem::appendRestartLog(const char* reason) {
    char reasonCopy[64];
    esp32base_internal::copySafe(reasonCopy, sizeof(reasonCopy), reason ? reason : "restart");

    const uint8_t nextCount = g_restartLogCount < 255 ? static_cast<uint8_t>(g_restartLogCount + 1U) : g_restartLogCount;
    const uint8_t index = nextCount % RESTART_LOG_CAPACITY;
    char key[15];
    snprintf(key, sizeof(key), "restart_%u", static_cast<unsigned>(index));

    const bool reasonOk = Esp32BaseConfig::setStr("eb_sys", key, reasonCopy);
    const bool countOk = Esp32BaseConfig::setInt("eb_sys", "rst_cnt", nextCount);
    if (countOk) {
        g_restartLogCount = nextCount;
    }
    return reasonOk && countOk;
}

uint8_t Esp32BaseSystem::restartLogCount() {
    return g_restartLogCount;
}
