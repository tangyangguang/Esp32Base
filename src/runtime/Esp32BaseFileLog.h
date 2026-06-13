#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

#include "../core/Esp32BaseLog.h"

#ifndef ESP32BASE_EB_FILELOG_PATH
#define ESP32BASE_EB_FILELOG_PATH "/esp32base/logs/system.log"
#endif

#ifndef ESP32BASE_EB_FILELOG_MAX_BYTES
#define ESP32BASE_EB_FILELOG_MAX_BYTES (32UL * 1024UL)
#endif

#ifndef ESP32BASE_EB_FILELOG_ROTATE_FILES
#define ESP32BASE_EB_FILELOG_ROTATE_FILES 4
#endif

#define ESP32BASE_FILELOG_MODE_OFF 0
#define ESP32BASE_FILELOG_MODE_ERROR ESP32BASE_LOG_ERROR
#define ESP32BASE_FILELOG_MODE_WARN ESP32BASE_LOG_WARN
#define ESP32BASE_FILELOG_MODE_INFO ESP32BASE_LOG_INFO

#ifndef ESP32BASE_EB_FILELOG_DEFAULT_MODE
#define ESP32BASE_EB_FILELOG_DEFAULT_MODE ESP32BASE_FILELOG_MODE_ERROR
#endif

#ifndef ESP32BASE_EB_FILELOG_BUFFER_SIZE
#define ESP32BASE_EB_FILELOG_BUFFER_SIZE 1024
#endif

#ifndef ESP32BASE_EB_FILELOG_FLUSH_INTERVAL_MS
#define ESP32BASE_EB_FILELOG_FLUSH_INTERVAL_MS 2000
#endif

#if ESP32BASE_EB_FILELOG_BUFFER_SIZE > 1024
#error "ESP32BASE_EB_FILELOG_BUFFER_SIZE must be <= 1024"
#endif

#if ESP32BASE_EB_FILELOG_DEFAULT_MODE != ESP32BASE_FILELOG_MODE_OFF && ESP32BASE_EB_FILELOG_DEFAULT_MODE != ESP32BASE_FILELOG_MODE_ERROR && ESP32BASE_EB_FILELOG_DEFAULT_MODE != ESP32BASE_FILELOG_MODE_WARN && ESP32BASE_EB_FILELOG_DEFAULT_MODE != ESP32BASE_FILELOG_MODE_INFO
#error "ESP32BASE_EB_FILELOG_DEFAULT_MODE must be ESP32BASE_FILELOG_MODE_OFF, ESP32BASE_FILELOG_MODE_ERROR, ESP32BASE_FILELOG_MODE_WARN, or ESP32BASE_FILELOG_MODE_INFO"
#endif

#if ESP32BASE_EB_FILELOG_DEFAULT_MODE > ESP32BASE_LOG_LEVEL
#error "ESP32BASE_EB_FILELOG_DEFAULT_MODE cannot exceed ESP32BASE_LOG_LEVEL"
#endif

class Esp32BaseFileLog {
public:
    static constexpr size_t PATH_BUFFER_SIZE = 96;
    static constexpr size_t SEGMENT_PATH_BUFFER_SIZE = PATH_BUFFER_SIZE + 4;

    enum Mode : uint8_t {
        OFF = ESP32BASE_FILELOG_MODE_OFF,
        ERROR = ESP32BASE_FILELOG_MODE_ERROR,
        WARN = ESP32BASE_FILELOG_MODE_WARN,
        INFO = ESP32BASE_FILELOG_MODE_INFO
    };

    using SegmentChunkFn = void (*)(const char* data, size_t len, void* user);

    static bool begin();
    static bool setMode(Mode mode);
    static bool flush();
    static bool clear();
    static void handle();

    static bool isEnabled();
    static bool faulted();
    static Mode mode();
    static const char* modeName();
    static const char* path();
    static uint32_t maxBytes();
    static uint8_t rotateFiles();
    static uint32_t size();
    static uint32_t segmentSize(uint8_t index);
    static bool bufferEnabled();
    static uint16_t bufferSize();
    static uint16_t bufferUsed();
    static uint32_t flushIntervalMs();

    static bool segmentPath(uint8_t index, char* out, size_t len);
    static bool streamSegment(uint8_t index, SegmentChunkFn cb, void* user = nullptr);
};
