#include "../Esp32BaseProfile.h"

#if ESP32BASE_ENABLE_WATCHDOG

#include "Esp32BaseWatchdog.h"
#include "../core/Esp32BaseConfig.h"
#include "../core/Esp32BaseLog.h"

#include <esp_system.h>
#include <esp_task_wdt.h>
#include <freertos/task.h>

namespace {
bool g_enabled = false;
TaskHandle_t g_longOperationTask = nullptr;
uint8_t g_longOperationDepth = 0;

uint32_t loadLifetimeResetCount() {
    const int32_t stored = Esp32BaseConfig::getInt("eb_sys", "wdt_cnt", -1);
    if (stored >= 0) {
        return static_cast<uint32_t>(stored);
    }
    const int32_t fallback = Esp32BaseConfig::getInt("eb_sys", "wdt_total_cnt", -1);
    if (fallback >= 0) {
        const uint32_t value = static_cast<uint32_t>(fallback);
        Esp32BaseConfig::setInt("eb_sys", "wdt_cnt", static_cast<int32_t>(value));
        return value;
    }
    return 0;
}

uint32_t g_lifetimeResetCount = 0;
}

bool Esp32BaseWatchdog::begin(uint32_t timeoutMs) {
    if (timeoutMs < 1000U) {
        ESP32BASE_LOG_W("watchdog", "invalid timeout_ms=%lu min=1000", static_cast<unsigned long>(timeoutMs));
        return false;
    }
    if (wasWatchdogReset()) {
        g_lifetimeResetCount = loadLifetimeResetCount() + 1U;
        const bool ok = Esp32BaseConfig::setInt("eb_sys", "wdt_cnt", static_cast<int32_t>(g_lifetimeResetCount));
        ESP32BASE_LOG_W("watchdog", "boot after watchdog reset count=%lu persist=%s",
                        static_cast<unsigned long>(g_lifetimeResetCount),
                        ok ? "success" : "failed");
    } else {
        g_lifetimeResetCount = loadLifetimeResetCount();
    }
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    esp_task_wdt_config_t config = {};
    config.timeout_ms = timeoutMs;
    config.idle_core_mask = 0;
    config.trigger_panic = true;
    if (esp_task_wdt_init(&config) != ESP_OK) {
        return false;
    }
#else
    if (esp_task_wdt_init(timeoutMs / 1000U, true) != ESP_OK) {
        return false;
    }
#endif
    if (esp_task_wdt_add(nullptr) != ESP_OK) {
        return false;
    }
    g_enabled = true;
    return true;
}

void Esp32BaseWatchdog::feed() {
    if (!g_enabled) {
        return;
    }
    esp_task_wdt_reset();
}

bool Esp32BaseWatchdog::enterLongOperation() {
    if (!g_enabled) {
        return true;
    }
    TaskHandle_t current = xTaskGetCurrentTaskHandle();
    if (g_longOperationTask == current) {
        if (g_longOperationDepth < UINT8_MAX) {
            ++g_longOperationDepth;
        }
        return true;
    }
    if (g_longOperationTask) {
        return false;
    }
    g_longOperationTask = current;
    g_longOperationDepth = 1;
    esp_task_wdt_reset();
    return true;
}

bool Esp32BaseWatchdog::exitLongOperation() {
    if (!g_enabled) {
        return true;
    }
    TaskHandle_t current = xTaskGetCurrentTaskHandle();
    if (!g_longOperationTask) {
        return true;
    }
    if (g_longOperationTask != current) {
        return false;
    }
    if (g_longOperationDepth > 1) {
        --g_longOperationDepth;
        return true;
    }
    esp_task_wdt_reset();
    g_longOperationTask = nullptr;
    g_longOperationDepth = 0;
    return true;
}

bool Esp32BaseWatchdog::currentTaskInLongOperation() {
    return g_longOperationTask && xTaskGetCurrentTaskHandle() == g_longOperationTask;
}

bool Esp32BaseWatchdog::isEnabled() {
    return g_enabled;
}

bool Esp32BaseWatchdog::wasWatchdogReset() {
    const esp_reset_reason_t reason = esp_reset_reason();
    return reason == ESP_RST_TASK_WDT || reason == ESP_RST_INT_WDT || reason == ESP_RST_WDT;
}

uint32_t Esp32BaseWatchdog::lifetimeResetCount() {
    return g_lifetimeResetCount;
}

#endif
