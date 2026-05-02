#ifndef ESP32_BASE_LOG_H
#define ESP32_BASE_LOG_H

#include <Arduino.h>
#include <stdarg.h>
#include <stdint.h>

#ifndef ESP32BASE_LOG_LEVEL
#define ESP32BASE_LOG_LEVEL 0
#endif

namespace Esp32BaseLog {

enum Level : uint8_t {
    DEBUG = 0,
    INFO = 1,
    WARN = 2,
    ERROR = 3
};

inline char levelChar(uint8_t level) {
    switch (level) {
        case DEBUG:
            return 'D';
        case INFO:
            return 'I';
        case WARN:
            return 'W';
        case ERROR:
            return 'E';
        default:
            return '?';
    }
}

inline void begin(uint32_t baud = 115200U) {
    if (!Serial) {
        Serial.begin(baud);
    }
}

inline void vlog(uint8_t level, const char* tag, const char* format, va_list args) {
    char message[192];
    vsnprintf(message, sizeof(message), format == nullptr ? "" : format, args);

    Serial.printf("[%10lu] %c/%s: %s\r\n",
                  static_cast<unsigned long>(millis()),
                  levelChar(level),
                  tag == nullptr ? "" : tag,
                  message);
}

inline void log(uint8_t level, const char* tag, const char* format, ...) {
    va_list args;
    va_start(args, format);
    vlog(level, tag, format, args);
    va_end(args);
}

}  // namespace Esp32BaseLog

#if ESP32BASE_LOG_LEVEL <= 0
#define ESP32BASE_LOG_D(tag, format, ...) Esp32BaseLog::log(Esp32BaseLog::DEBUG, tag, format, ##__VA_ARGS__)
#else
#define ESP32BASE_LOG_D(tag, format, ...) ((void)0)
#endif

#if ESP32BASE_LOG_LEVEL <= 1
#define ESP32BASE_LOG_I(tag, format, ...) Esp32BaseLog::log(Esp32BaseLog::INFO, tag, format, ##__VA_ARGS__)
#else
#define ESP32BASE_LOG_I(tag, format, ...) ((void)0)
#endif

#if ESP32BASE_LOG_LEVEL <= 2
#define ESP32BASE_LOG_W(tag, format, ...) Esp32BaseLog::log(Esp32BaseLog::WARN, tag, format, ##__VA_ARGS__)
#else
#define ESP32BASE_LOG_W(tag, format, ...) ((void)0)
#endif

#if ESP32BASE_LOG_LEVEL <= 3
#define ESP32BASE_LOG_E(tag, format, ...) Esp32BaseLog::log(Esp32BaseLog::ERROR, tag, format, ##__VA_ARGS__)
#else
#define ESP32BASE_LOG_E(tag, format, ...) ((void)0)
#endif

#endif
