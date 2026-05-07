#pragma once

#include <Arduino.h>
#include <stdint.h>

#ifndef ESP32BASE_NTP_GMT_OFFSET_SEC
#define ESP32BASE_NTP_GMT_OFFSET_SEC (8L * 3600L)
#endif

#ifndef ESP32BASE_NTP_DAYLIGHT_OFFSET_SEC
#define ESP32BASE_NTP_DAYLIGHT_OFFSET_SEC 0L
#endif

#ifndef ESP32BASE_NTP_SYNC_MIN_EPOCH
#define ESP32BASE_NTP_SYNC_MIN_EPOCH 1700000000UL
#endif

class Esp32BaseNtp {
public:
    static bool begin();
    static bool isStarted();
    static bool isTimeSynced();
    static uint32_t timestamp();
    static void setServers(const char* s1, const char* s2 = nullptr, const char* s3 = nullptr);
    static bool formatTime(char* out, size_t len, const char* fmt);
    static const char* logTimeString();
};
