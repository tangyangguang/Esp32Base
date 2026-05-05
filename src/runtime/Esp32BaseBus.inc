#include "../Esp32BaseProfile.h"

#if ESP32BASE_ENABLE_BUS

#include "Esp32BaseBus.h"
#include "../core/Esp32BaseLog.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>

namespace {
struct Subscription {
    Esp32BaseBus::SubscriptionId id;
    char event[32];
    Esp32BaseBus::Callback cb;
    void* user;
};

TaskHandle_t g_loopTask = nullptr;
Subscription g_subs[ESP32BASE_BUS_MAX_SUBSCRIBERS];
Esp32BaseBus::SubscriptionId g_nextId = 1;

bool matchEvent(const char* pattern, const char* event) {
    return pattern && event && strcmp(pattern, event) == 0;
}
}

bool Esp32BaseBus::begin() {
    g_loopTask = xTaskGetCurrentTaskHandle();
    return true;
}

Esp32BaseBus::SubscriptionId Esp32BaseBus::subscribe(const char* event, Callback cb, void* user) {
    if (!event || !cb || strlen(event) >= sizeof(g_subs[0].event)) {
        return 0;
    }
    for (uint8_t i = 0; i < ESP32BASE_BUS_MAX_SUBSCRIBERS; ++i) {
        if (g_subs[i].id == 0) {
            g_subs[i].id = g_nextId++;
            if (g_nextId == 0) {
                g_nextId = 1;
            }
            strlcpy(g_subs[i].event, event, sizeof(g_subs[i].event));
            g_subs[i].cb = cb;
            g_subs[i].user = user;
            return g_subs[i].id;
        }
    }
    return 0;
}

bool Esp32BaseBus::publish(const char* event, const char* data) {
    if (!event) {
        return false;
    }
    if (g_loopTask && xTaskGetCurrentTaskHandle() != g_loopTask) {
#if defined(ESP32BASE_BUS_STRICT_TASK_CHECK) && ESP32BASE_BUS_STRICT_TASK_CHECK
        configASSERT(false);
#endif
        ESP32BASE_LOG_E("bus", "publish from non-loop task: %s", event);
        return false;
    }

    for (uint8_t i = 0; i < ESP32BASE_BUS_MAX_SUBSCRIBERS; ++i) {
        if (g_subs[i].id != 0 && matchEvent(g_subs[i].event, event)) {
            g_subs[i].cb(event, data ? data : "", g_subs[i].user);
        }
    }
    return true;
}

bool Esp32BaseBus::unsubscribe(SubscriptionId id) {
    if (id == 0) {
        return false;
    }
    for (uint8_t i = 0; i < ESP32BASE_BUS_MAX_SUBSCRIBERS; ++i) {
        if (g_subs[i].id == id) {
            g_subs[i] = Subscription{};
            return true;
        }
    }
    return false;
}

uint16_t Esp32BaseBus::subscriberCount() {
    uint16_t count = 0;
    for (uint8_t i = 0; i < ESP32BASE_BUS_MAX_SUBSCRIBERS; ++i) {
        if (g_subs[i].id != 0) {
            ++count;
        }
    }
    return count;
}

uint16_t Esp32BaseBus::subscriberCapacity() {
    return ESP32BASE_BUS_MAX_SUBSCRIBERS;
}

#endif
