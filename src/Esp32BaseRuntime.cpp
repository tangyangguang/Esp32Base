#include "Esp32BaseRuntime.h"

#if !defined(ESP32)
#error "Esp32Base supports ESP32 Arduino Core targets only."
#endif

#include "Esp32BaseConfig.h"
#include "Esp32BaseLog.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <esp_heap_caps.h>
#include <esp_sleep.h>
#include <esp_system.h>
#include <esp_task_wdt.h>

bool Esp32BaseRuntime::_ready = false;
bool Esp32BaseRuntime::_fsReady = false;
bool Esp32BaseRuntime::_watchdogEnabled = false;
uint32_t Esp32BaseRuntime::_watchdogTimeoutMs = ESP32BASE_WATCHDOG_TIMEOUT_MS;
uint32_t Esp32BaseRuntime::_watchdogCount = 0;

namespace {

constexpr const char* kRuntimeNs = Esp32BaseConfig::RESERVED_NAMESPACE;
constexpr const char* kWatchdogKey = "wdt_count";

const char* reset_reason_to_text(esp_reset_reason_t reason) {
    switch (reason) {
        case ESP_RST_POWERON:
            return "power_on";
        case ESP_RST_EXT:
            return "external";
        case ESP_RST_SW:
            return "software";
        case ESP_RST_PANIC:
            return "panic";
        case ESP_RST_INT_WDT:
            return "int_wdt";
        case ESP_RST_TASK_WDT:
            return "task_wdt";
        case ESP_RST_WDT:
            return "other_wdt";
        case ESP_RST_DEEPSLEEP:
            return "deep_sleep";
        case ESP_RST_BROWNOUT:
            return "brownout";
        case ESP_RST_SDIO:
            return "sdio";
        case ESP_RST_UNKNOWN:
        default:
            return "unknown";
    }
}

const char* wake_reason_to_text(esp_sleep_wakeup_cause_t reason) {
    switch (reason) {
        case ESP_SLEEP_WAKEUP_EXT0:
            return "ext0";
        case ESP_SLEEP_WAKEUP_EXT1:
            return "ext1";
        case ESP_SLEEP_WAKEUP_TIMER:
            return "timer";
        case ESP_SLEEP_WAKEUP_TOUCHPAD:
            return "touchpad";
        case ESP_SLEEP_WAKEUP_ULP:
            return "ulp";
        case ESP_SLEEP_WAKEUP_GPIO:
            return "gpio";
        case ESP_SLEEP_WAKEUP_UART:
            return "uart";
        case ESP_SLEEP_WAKEUP_UNDEFINED:
        default:
            return "undefined";
    }
}

}  // namespace

bool Esp32BaseRuntime::begin() {
    if (_ready) {
        return true;
    }

    _watchdogCount = static_cast<uint32_t>(Esp32BaseConfig::getInt(kRuntimeNs, kWatchdogKey, 0));
    if (wasWatchdogReset()) {
        ++_watchdogCount;
        storeWatchdogCount(_watchdogCount);
    }

    mountFileSystem();
    _ready = true;
    logConfig();
    return true;
}

void Esp32BaseRuntime::handle() {
    feedWatchdog();
}

bool Esp32BaseRuntime::isReady() {
    return _ready;
}

const char* Esp32BaseRuntime::resetReason() {
    return reset_reason_to_text(esp_reset_reason());
}

const char* Esp32BaseRuntime::wakeReason() {
    return wake_reason_to_text(esp_sleep_get_wakeup_cause());
}

uint32_t Esp32BaseRuntime::freeHeap() {
    return static_cast<uint32_t>(ESP.getFreeHeap());
}

uint32_t Esp32BaseRuntime::totalHeap() {
    return static_cast<uint32_t>(heap_caps_get_total_size(MALLOC_CAP_8BIT));
}

uint32_t Esp32BaseRuntime::flashSize() {
    return static_cast<uint32_t>(ESP.getFlashChipSize());
}

void Esp32BaseRuntime::restart() {
    Esp32BaseConfig::flush();
    ESP32BASE_LOG_I("BaseRun", "restart requested");
    delay(50);
    ESP.restart();
}

void Esp32BaseRuntime::deepSleepSeconds(uint32_t seconds) {
    deepSleepUs(static_cast<uint64_t>(seconds) * 1000000ULL);
}

void Esp32BaseRuntime::deepSleepUs(uint64_t us) {
    Esp32BaseConfig::flush();
    ESP32BASE_LOG_I("BaseRun", "deep_sleep us=%llu", static_cast<unsigned long long>(us));
    if (_watchdogEnabled) {
        disableWatchdog();
    }
    delay(50);
    if (us > 0ULL) {
        esp_sleep_enable_timer_wakeup(us);
    }
    esp_deep_sleep_start();
}

bool Esp32BaseRuntime::enableWatchdog(uint32_t timeoutMs) {
    if (timeoutMs < 1000UL) {
        timeoutMs = 1000UL;
    } else if (timeoutMs > 60000UL) {
        timeoutMs = 60000UL;
    }

    _watchdogTimeoutMs = timeoutMs;
    uint32_t timeoutSeconds = (timeoutMs + 999UL) / 1000UL;

    esp_err_t initResult = esp_task_wdt_init(timeoutSeconds, true);
    if (initResult != ESP_OK && initResult != ESP_ERR_INVALID_STATE) {
        ESP32BASE_LOG_E("BaseRun", "wdt init failed err=%ld", static_cast<long>(initResult));
        return false;
    }

    esp_err_t addResult = esp_task_wdt_add(nullptr);
    if (addResult != ESP_OK && addResult != ESP_ERR_INVALID_STATE) {
        ESP32BASE_LOG_E("BaseRun", "wdt add failed err=%ld", static_cast<long>(addResult));
        return false;
    }

    _watchdogEnabled = true;
    ESP32BASE_LOG_I("BaseRun", "wdt enabled timeout=%lu", static_cast<unsigned long>(_watchdogTimeoutMs));
    return true;
}

void Esp32BaseRuntime::disableWatchdog() {
    if (!_watchdogEnabled) {
        return;
    }

    esp_task_wdt_delete(nullptr);
    _watchdogEnabled = false;
    ESP32BASE_LOG_I("BaseRun", "wdt disabled");
}

void Esp32BaseRuntime::feedWatchdog() {
    if (_watchdogEnabled) {
        esp_task_wdt_reset();
    }
}

bool Esp32BaseRuntime::isWatchdogEnabled() {
    return _watchdogEnabled;
}

bool Esp32BaseRuntime::wasWatchdogReset() {
    esp_reset_reason_t reason = esp_reset_reason();
    return reason == ESP_RST_TASK_WDT || reason == ESP_RST_INT_WDT || reason == ESP_RST_WDT;
}

uint32_t Esp32BaseRuntime::watchdogCount() {
    return _watchdogCount;
}

void Esp32BaseRuntime::clearWatchdogCount() {
    _watchdogCount = 0;
    storeWatchdogCount(0);
}

bool Esp32BaseRuntime::writeFile(const char* path, const char* content) {
    if (!_fsReady || path == nullptr || path[0] != '/') {
        return false;
    }

    File file = LittleFS.open(path, "w");
    if (!file) {
        ESP32BASE_LOG_W("BaseRun", "file open write failed path=%s", path);
        return false;
    }

    size_t written = file.print(content == nullptr ? "" : content);
    file.close();
    return written == strlen(content == nullptr ? "" : content);
}

bool Esp32BaseRuntime::readFile(const char* path, char* out, size_t len) {
    if (out == nullptr || len == 0U) {
        return false;
    }

    out[0] = '\0';
    if (!_fsReady || path == nullptr || path[0] != '/') {
        return false;
    }

    File file = LittleFS.open(path, "r");
    if (!file) {
        return false;
    }

    size_t limit = len - 1U;
    if (limit > ESP32BASE_FILE_READ_MAX) {
        limit = ESP32BASE_FILE_READ_MAX;
    }

    size_t readLen = file.readBytes(out, limit);
    out[readLen] = '\0';
    file.close();
    return true;
}

bool Esp32BaseRuntime::fileExists(const char* path) {
    return _fsReady && path != nullptr && LittleFS.exists(path);
}

bool Esp32BaseRuntime::removeFile(const char* path) {
    return _fsReady && path != nullptr && LittleFS.remove(path);
}

size_t Esp32BaseRuntime::fsTotalBytes() {
    return _fsReady ? LittleFS.totalBytes() : 0U;
}

size_t Esp32BaseRuntime::fsUsedBytes() {
    return _fsReady ? LittleFS.usedBytes() : 0U;
}

size_t Esp32BaseRuntime::fsFreeBytes() {
    size_t total = fsTotalBytes();
    size_t used = fsUsedBytes();
    return total > used ? total - used : 0U;
}

void Esp32BaseRuntime::logConfig() {
    ESP32BASE_LOG_I("BaseRun", "ready=%u heap=%lu/%lu flash=%lu reset=%s wake=%s fs=%u:%u/%u wdt=%u/%lu count=%lu",
                    _ready ? 1U : 0U,
                    static_cast<unsigned long>(freeHeap()),
                    static_cast<unsigned long>(totalHeap()),
                    static_cast<unsigned long>(flashSize()),
                    resetReason(),
                    wakeReason(),
                    _fsReady ? 1U : 0U,
                    static_cast<unsigned>(fsUsedBytes()),
                    static_cast<unsigned>(fsTotalBytes()),
                    _watchdogEnabled ? 1U : 0U,
                    static_cast<unsigned long>(_watchdogTimeoutMs),
                    static_cast<unsigned long>(_watchdogCount));
}

bool Esp32BaseRuntime::mountFileSystem() {
    if (_fsReady) {
        return true;
    }

    _fsReady = LittleFS.begin(true);
    if (!_fsReady) {
        ESP32BASE_LOG_W("BaseRun", "littlefs mount failed");
    }
    return _fsReady;
}

void Esp32BaseRuntime::storeWatchdogCount(uint32_t count) {
    Esp32BaseConfig::setInt(kRuntimeNs, kWatchdogKey, static_cast<int32_t>(count));
}
