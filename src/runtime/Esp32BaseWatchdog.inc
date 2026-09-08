#include "../Esp32BaseProfile.h"

#if ESP32BASE_ENABLE_WATCHDOG

#include "Esp32BaseWatchdog.h"
#include "../core/Esp32BaseConfig.h"
#include "../core/Esp32BaseLog.h"

#include <sdkconfig.h>
#include <esp_system.h>
#include <esp_task_wdt.h>
#include <freertos/task.h>

namespace {
bool g_enabled = false;
TaskHandle_t g_watchdogTask = nullptr;
bool g_resetCountLoaded = false;
TaskHandle_t g_longOperationTask = nullptr;
uint8_t g_longOperationDepth = 0;

uint32_t loadLifetimeResetCount() {
    const int32_t stored = Esp32BaseConfig::getInt("eb_sys", "wdt_cnt", -1);
    if (stored >= 0) {
        return static_cast<uint32_t>(stored);
    }
    return 0;
}

uint32_t g_lifetimeResetCount = 0;
}

bool Esp32BaseWatchdog::begin() {
    const TaskHandle_t current = xTaskGetCurrentTaskHandle();
    if (g_enabled) {
        return current == g_watchdogTask && esp_task_wdt_status(nullptr) == ESP_OK;
    }
    if (!g_resetCountLoaded) {
        g_resetCountLoaded = true;
        g_lifetimeResetCount = loadLifetimeResetCount();
        if (wasWatchdogReset()) {
            ++g_lifetimeResetCount;
            const bool ok = Esp32BaseConfig::setInt("eb_sys", "wdt_cnt", static_cast<int32_t>(g_lifetimeResetCount));
            ESP32BASE_LOG_W("watchdog", "boot after watchdog reset count=%lu persist=%s",
                            static_cast<unsigned long>(g_lifetimeResetCount), ok ? "success" : "failed");
        }
    }
    esp_err_t status = esp_task_wdt_status(nullptr);
    if (status == ESP_ERR_INVALID_STATE) {
        // Configure only an absent system watchdog. Never reconfigure existing
        // subscribers, idle-task protection, timeout or panic policy.
#if ESP_ARDUINO_VERSION_MAJOR >= 3
        esp_task_wdt_config_t config = {};
        config.timeout_ms = CONFIG_ESP_TASK_WDT_TIMEOUT_S * 1000U;
#if defined(CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0) && CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0
        config.idle_core_mask |= 1U;
#endif
#if defined(CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU1) && CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU1
        config.idle_core_mask |= 2U;
#endif
        config.trigger_panic = true;
        status = esp_task_wdt_init(&config);
#else
        status = esp_task_wdt_init(CONFIG_ESP_TASK_WDT_TIMEOUT_S, true);
#endif
        if (status != ESP_OK) {
            ESP32BASE_LOG_E("watchdog", "init failed error=%d", static_cast<int>(status));
            return false;
        }
        status = esp_task_wdt_status(nullptr);
    }
    if (status == ESP_ERR_NOT_FOUND) {
        status = esp_task_wdt_add(nullptr);
    }
    if (status != ESP_OK) {
        ESP32BASE_LOG_E("watchdog", "subscribe failed error=%d", static_cast<int>(status));
        return false;
    }
    g_watchdogTask = current;
    g_enabled = true;
    return true;
}

void Esp32BaseWatchdog::feed() {
    if (!g_enabled || xTaskGetCurrentTaskHandle() != g_watchdogTask) {
        return;
    }
    esp_task_wdt_reset();
}

bool Esp32BaseWatchdog::enterLongOperation() {
    if (!g_enabled) {
        return true;
    }
    TaskHandle_t current = xTaskGetCurrentTaskHandle();
    if (current != g_watchdogTask) {
        return false;
    }
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
    if (current != g_watchdogTask) {
        return false;
    }
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
