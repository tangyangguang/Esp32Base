#pragma once

#include <Arduino.h>
#include <stdint.h>

#ifndef ESP32BASE_BUS_MAX_SUBSCRIBERS
#if defined(CONFIG_IDF_TARGET_ESP32C3)
#define ESP32BASE_BUS_MAX_SUBSCRIBERS 12
#else
#define ESP32BASE_BUS_MAX_SUBSCRIBERS 16
#endif
#endif

class Esp32BaseBus {
public:
    using Callback = void (*)(const char* event, const char* data, void* user);
    using SubscriptionId = uint16_t;

    static bool begin();
    static SubscriptionId subscribe(const char* event, Callback cb, void* user = nullptr);
    static bool publish(const char* event, const char* data = "");
    static bool unsubscribe(SubscriptionId id);
    static uint16_t subscriberCount();
    static uint16_t subscriberCapacity();
};
