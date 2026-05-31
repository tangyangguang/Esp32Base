#pragma once

#include <esp_sleep.h>
#include <esp_system.h>

namespace esp32base_internal {

inline const char* resetReasonName(esp_reset_reason_t reason) {
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

inline const char* resetReasonText(esp_reset_reason_t reason) {
    switch (reason) {
        case ESP_RST_POWERON: return "上电启动";
        case ESP_RST_EXT: return "外部复位";
        case ESP_RST_SW: return "软件重启";
        case ESP_RST_PANIC: return "异常崩溃";
        case ESP_RST_INT_WDT:
        case ESP_RST_TASK_WDT:
        case ESP_RST_WDT: return "看门狗复位";
        case ESP_RST_DEEPSLEEP: return "深度睡眠唤醒";
        case ESP_RST_BROWNOUT: return "电压跌落复位";
        case ESP_RST_SDIO: return "SDIO复位";
        case ESP_RST_UNKNOWN:
        default: return "未知复位原因";
    }
}

inline const char* wakeReasonName(esp_sleep_wakeup_cause_t cause) {
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

inline const char* wakeReasonText(esp_sleep_wakeup_cause_t cause) {
    switch (cause) {
        case ESP_SLEEP_WAKEUP_EXT0: return "EXT0外部唤醒";
        case ESP_SLEEP_WAKEUP_EXT1: return "EXT1外部唤醒";
        case ESP_SLEEP_WAKEUP_TIMER: return "定时器唤醒";
        case ESP_SLEEP_WAKEUP_TOUCHPAD: return "触摸唤醒";
        case ESP_SLEEP_WAKEUP_ULP: return "ULP唤醒";
#if defined(ESP_SLEEP_WAKEUP_GPIO)
        case ESP_SLEEP_WAKEUP_GPIO: return "GPIO唤醒";
#endif
#if defined(ESP_SLEEP_WAKEUP_UART)
        case ESP_SLEEP_WAKEUP_UART: return "UART唤醒";
#endif
        case ESP_SLEEP_WAKEUP_UNDEFINED:
        default: return "非睡眠唤醒";
    }
}

} // namespace esp32base_internal
