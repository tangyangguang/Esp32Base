#include "../Esp32BaseProfile.h"

#if ESP32BASE_ENABLE_SLEEP

#include "Esp32BaseSleep.h"
#include "../core/Esp32BaseConfig.h"
#include "../core/Esp32BaseLog.h"
#include "../core/Esp32BaseSystem.h"
#if ESP32BASE_ENABLE_FILELOG
#include "Esp32BaseFileLog.h"
#endif

#include <esp_sleep.h>

namespace {
bool g_sleepReady = false;
}

bool Esp32BaseSleep::begin() {
    g_sleepReady = true;
    return true;
}

bool Esp32BaseSleep::deepSleepSeconds(uint32_t seconds) {
    return deepSleepUs(static_cast<uint64_t>(seconds) * 1000000ULL);
}

bool Esp32BaseSleep::deepSleepUs(uint64_t us) {
    Esp32BaseSystem::appendRestartLog("deep_sleep");
    Esp32BaseConfig::flushAll();
#if ESP32BASE_ENABLE_FILELOG
    Esp32BaseFileLog::flush();
#endif
    ESP32BASE_LOG_I("sleep", "enter deep sleep: %llu us", static_cast<unsigned long long>(us));
#if ESP32BASE_ENABLE_FILELOG
    Esp32BaseFileLog::flush();
#endif
    if (us > 0) {
        esp_sleep_enable_timer_wakeup(us);
    }
    delay(20);
    esp_deep_sleep_start();
    return true;
}

bool Esp32BaseSleep::enableTimerWakeup(uint64_t us) {
    return esp_sleep_enable_timer_wakeup(us) == ESP_OK;
}

bool Esp32BaseSleep::enableGpioWakeup(int gpio, int level) {
#if defined(CONFIG_IDF_TARGET_ESP32C3)
    (void)gpio;
    (void)level;
    ESP32BASE_LOG_W("sleep", "gpio wakeup not enabled for this target");
    return false;
#else
    return esp_sleep_enable_ext0_wakeup(static_cast<gpio_num_t>(gpio), level) == ESP_OK;
#endif
}

#endif
