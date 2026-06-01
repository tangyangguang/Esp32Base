#include "Esp32BaseSystem.h"

#include "Esp32BaseConfig.h"
#include "Esp32BaseLog.h"
#include "Esp32BaseResetReason.h"
#include "Esp32BaseUtil.h"

#include <Preferences.h>
#include <esp_system.h>
#include <esp_sleep.h>

namespace {
bool g_ready = false;
uint32_t g_bootCount = 0;
uint8_t g_restartLogCount = 0;
Esp32BaseSystem::PreLifecycleHook g_preRestartHook = nullptr;
Esp32BaseSystem::PreLifecycleHook g_preSleepHook = nullptr;

constexpr uint8_t RESTART_LOG_CAPACITY = 4;

void runPreRestartHook() {
    if (g_preRestartHook) {
        g_preRestartHook();
    }
}
}

bool Esp32BaseSystem::begin() {
    Preferences prefs;
    if (prefs.begin("eb_sys", false)) {
        const uint32_t previousBootCount = prefs.getUInt("boot_cnt", 0);
        g_bootCount = previousBootCount + 1U;
        if (prefs.putUInt("boot_cnt", g_bootCount) == 0) {
            ESP32BASE_LOG_W("system", "boot_count save failed");
        }
        prefs.end();
    } else {
        g_bootCount = 0;
        ESP32BASE_LOG_W("system", "boot_count storage unavailable");
    }
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

uint32_t Esp32BaseSystem::bootCount() {
    return g_bootCount;
}

const char* Esp32BaseSystem::resetReason() {
    return esp32base_internal::resetReasonName(esp_reset_reason());
}

const char* Esp32BaseSystem::resetReasonText() {
    return esp32base_internal::resetReasonText(esp_reset_reason());
}

const char* Esp32BaseSystem::wakeReason() {
    return esp32base_internal::wakeReasonName(esp_sleep_get_wakeup_cause());
}

const char* Esp32BaseSystem::wakeReasonText() {
    return esp32base_internal::wakeReasonText(esp_sleep_get_wakeup_cause());
}

void Esp32BaseSystem::restart(const char* reason) {
    appendRestartLog(reason ? reason : "restart");
    Esp32BaseConfig::flushAll();
    ESP32BASE_LOG_W("system", "restart: %s", reason ? reason : "");
    runPreRestartHook();
    delay(50);
    ESP.restart();
}

void Esp32BaseSystem::setPreRestartHook(PreLifecycleHook hook) {
    g_preRestartHook = hook;
}

void Esp32BaseSystem::setPreSleepHook(PreLifecycleHook hook) {
    g_preSleepHook = hook;
}

void Esp32BaseSystem::runPreSleepHook() {
    if (g_preSleepHook) {
        g_preSleepHook();
    }
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
