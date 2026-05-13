#include "../Esp32BaseProfile.h"

#if ESP32BASE_ENABLE_WATCHDOG

#include "Esp32BaseWatchdog.h"
#include "../core/Esp32BaseConfig.h"
#include "../core/Esp32BaseLog.h"

#include <esp_system.h>
#include <esp_task_wdt.h>

namespace {
bool g_enabled = false;
bool g_removed = false;
uint32_t g_totalResetCount = 0;
uint32_t g_tripResetCount = 0;
}

bool Esp32BaseWatchdog::begin(uint32_t timeoutMs) {
    if (timeoutMs < 1000U) {
        ESP32BASE_LOG_W("watchdog", "invalid timeout_ms=%lu min=1000", static_cast<unsigned long>(timeoutMs));
        return false;
    }
    if (wasWatchdogReset()) {
        g_totalResetCount = static_cast<uint32_t>(Esp32BaseConfig::getInt("eb_sys", "wdt_total_cnt", 0)) + 1U;
        g_tripResetCount = static_cast<uint32_t>(Esp32BaseConfig::getInt("eb_sys", "wdt_trip_cnt", 0)) + 1U;
        const bool totalOk = Esp32BaseConfig::setInt("eb_sys", "wdt_total_cnt", static_cast<int32_t>(g_totalResetCount));
        const bool tripOk = Esp32BaseConfig::setInt("eb_sys", "wdt_trip_cnt", static_cast<int32_t>(g_tripResetCount));
        ESP32BASE_LOG_W("watchdog", "boot_after_watchdog_reset total_resets=%lu trip_resets=%lu persist=%s",
                        static_cast<unsigned long>(g_totalResetCount),
                        static_cast<unsigned long>(g_tripResetCount),
                        (totalOk && tripOk) ? "success" : "failed");
    } else {
        g_totalResetCount = static_cast<uint32_t>(Esp32BaseConfig::getInt("eb_sys", "wdt_total_cnt", 0));
        g_tripResetCount = static_cast<uint32_t>(Esp32BaseConfig::getInt("eb_sys", "wdt_trip_cnt", 0));
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
    if (g_enabled && !g_removed) {
        esp_task_wdt_reset();
    }
}

bool Esp32BaseWatchdog::removeCurrentTaskForLongOperation() {
    if (!g_enabled || g_removed) {
        return true;
    }
    if (esp_task_wdt_delete(nullptr) != ESP_OK) {
        return false;
    }
    g_removed = true;
    return true;
}

bool Esp32BaseWatchdog::restoreCurrentTaskAfterLongOperation() {
    if (!g_enabled || !g_removed) {
        return true;
    }
    if (esp_task_wdt_add(nullptr) != ESP_OK) {
        return false;
    }
    g_removed = false;
    return true;
}

bool Esp32BaseWatchdog::isEnabled() {
    return g_enabled;
}

bool Esp32BaseWatchdog::wasWatchdogReset() {
    const esp_reset_reason_t reason = esp_reset_reason();
    return reason == ESP_RST_TASK_WDT || reason == ESP_RST_INT_WDT || reason == ESP_RST_WDT;
}

uint32_t Esp32BaseWatchdog::totalResetCount() {
    return g_totalResetCount;
}

uint32_t Esp32BaseWatchdog::tripResetCount() {
    return g_tripResetCount;
}

bool Esp32BaseWatchdog::clearTripResetCount() {
    g_tripResetCount = 0;
    const bool ok = Esp32BaseConfig::setInt("eb_sys", "wdt_trip_cnt", 0);
    ESP32BASE_LOG_W("watchdog", "watchdog_trip_cleared result=%s", ok ? "success" : "failed");
    return ok;
}

#endif
