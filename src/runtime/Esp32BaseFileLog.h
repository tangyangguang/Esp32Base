#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

#include "../core/Esp32BaseLog.h"

#ifndef ESP32BASE_EB_FILELOG_PATH
#define ESP32BASE_EB_FILELOG_PATH "/logs/eb_app.log"
#endif

#ifndef ESP32BASE_EB_FILELOG_MAX_BYTES
#define ESP32BASE_EB_FILELOG_MAX_BYTES (32UL * 1024UL)
#endif

#ifndef ESP32BASE_EB_FILELOG_ROTATE_FILES
#define ESP32BASE_EB_FILELOG_ROTATE_FILES 4
#endif

#ifndef ESP32BASE_EB_FILELOG_LEVEL
#define ESP32BASE_EB_FILELOG_LEVEL ESP32BASE_LOG_WARN
#endif

#ifndef ESP32BASE_EB_FILELOG_BUFFER_SIZE
#define ESP32BASE_EB_FILELOG_BUFFER_SIZE 1024
#endif

#ifndef ESP32BASE_EB_FILELOG_FLUSH_INTERVAL_MS
#define ESP32BASE_EB_FILELOG_FLUSH_INTERVAL_MS 2000
#endif

#ifndef ESP32BASE_EB_FILELOG_ENABLED
#define ESP32BASE_EB_FILELOG_ENABLED 1
#endif

#if ESP32BASE_EB_FILELOG_BUFFER_SIZE > 1024
#error "ESP32BASE_EB_FILELOG_BUFFER_SIZE must be <= 1024"
#endif

class Esp32BaseFileLog {
public:
    using SegmentChunkFn = void (*)(const char* data, size_t len, void* user);

    static bool begin();
    static bool enable(const char* path = ESP32BASE_EB_FILELOG_PATH,
                       uint32_t maxBytes = ESP32BASE_EB_FILELOG_MAX_BYTES,
                       Esp32BaseLog::Level fileLevel = static_cast<Esp32BaseLog::Level>(ESP32BASE_EB_FILELOG_LEVEL),
                       uint8_t rotateFiles = ESP32BASE_EB_FILELOG_ROTATE_FILES);
    static void disable();
    static bool flush();
    static bool clear();
    static void handle();

    static bool isEnabled();
    static const char* path();
    static uint32_t maxBytes();
    static uint8_t rotateFiles();
    static Esp32BaseLog::Level level();
    static const char* levelName();
    static uint32_t size();
    static uint32_t segmentSize(uint8_t index);
    static bool bufferEnabled();
    static uint16_t bufferSize();
    static uint16_t bufferUsed();
    static uint32_t flushIntervalMs();

    static bool segmentPath(uint8_t index, char* out, size_t len);
    static bool streamSegment(uint8_t index, SegmentChunkFn cb, void* user = nullptr);
};
