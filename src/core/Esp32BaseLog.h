#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

#define ESP32BASE_LOG_NONE 0
#define ESP32BASE_LOG_ERROR 1
#define ESP32BASE_LOG_WARN 2
#define ESP32BASE_LOG_INFO 3
#define ESP32BASE_LOG_DEBUG 4
#define ESP32BASE_LOG_VERBOSE 5

#ifndef ESP32BASE_LOG_LEVEL
#define ESP32BASE_LOG_LEVEL ESP32BASE_LOG_INFO
#endif

#ifndef ESP32BASE_LOG_BAUD
#define ESP32BASE_LOG_BAUD 115200
#endif

class Esp32BaseLog {
public:
    enum Level : uint8_t {
        NONE = 0,
        ERROR = 1,
        WARN = 2,
        INFO = 3,
        DEBUG = 4,
        VERBOSE = 5
    };

    using Sink = void (*)(Level level, const char* tag, const char* message);
    using LineSink = void (*)(Level level, const char* tag, const char* message, const char* line);
    using TimeProvider = const char* (*)();

    static bool begin(uint32_t baud = ESP32BASE_LOG_BAUD);
    static void setRuntimeLevel(Level level);
    static Level runtimeLevel();
    static void setSerialLevel(Level level);
    static Level serialLevel();
    static void setSink(Sink sink);
    static void setInternalLineSink(LineSink sink);
    static void setTimeProvider(TimeProvider provider);

    static void write(Level level, const char* tag, const char* fmt, ...);
    static void formatBytes(uint64_t bytes, char* out, size_t len);
    static void formatMillis(uint32_t ms, char* out, size_t len);
    static void formatUptime(uint32_t ms, char* out, size_t len);
    static void formatUptime64(uint64_t ms, char* out, size_t len);
};

#if ESP32BASE_LOG_LEVEL >= ESP32BASE_LOG_ERROR
#define ESP32BASE_LOG_E(tag, fmt, ...) Esp32BaseLog::write(Esp32BaseLog::ERROR, tag, fmt, ##__VA_ARGS__)
#else
#define ESP32BASE_LOG_E(tag, fmt, ...) do {} while (0)
#endif

#if ESP32BASE_LOG_LEVEL >= ESP32BASE_LOG_WARN
#define ESP32BASE_LOG_W(tag, fmt, ...) Esp32BaseLog::write(Esp32BaseLog::WARN, tag, fmt, ##__VA_ARGS__)
#else
#define ESP32BASE_LOG_W(tag, fmt, ...) do {} while (0)
#endif

#if ESP32BASE_LOG_LEVEL >= ESP32BASE_LOG_INFO
#define ESP32BASE_LOG_I(tag, fmt, ...) Esp32BaseLog::write(Esp32BaseLog::INFO, tag, fmt, ##__VA_ARGS__)
#else
#define ESP32BASE_LOG_I(tag, fmt, ...) do {} while (0)
#endif

#if ESP32BASE_LOG_LEVEL >= ESP32BASE_LOG_DEBUG
#define ESP32BASE_LOG_D(tag, fmt, ...) Esp32BaseLog::write(Esp32BaseLog::DEBUG, tag, fmt, ##__VA_ARGS__)
#else
#define ESP32BASE_LOG_D(tag, fmt, ...) do {} while (0)
#endif

#if ESP32BASE_LOG_LEVEL >= ESP32BASE_LOG_VERBOSE
#define ESP32BASE_LOG_V(tag, fmt, ...) Esp32BaseLog::write(Esp32BaseLog::VERBOSE, tag, fmt, ##__VA_ARGS__)
#else
#define ESP32BASE_LOG_V(tag, fmt, ...) do {} while (0)
#endif
