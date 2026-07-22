#include "Esp32BaseLog.h"

#include <stdarg.h>
#include <stdio.h>

namespace {
Esp32BaseLog::Level g_runtimeLevel = static_cast<Esp32BaseLog::Level>(ESP32BASE_LOG_LEVEL);
Esp32BaseLog::Level g_serialLevel = static_cast<Esp32BaseLog::Level>(ESP32BASE_LOG_LEVEL);
Esp32BaseLog::Sink g_sink = nullptr;
Esp32BaseLog::LineSink g_internalLineSink = nullptr;
Esp32BaseLog::TimeProvider g_timeProvider = nullptr;

const char* levelName(Esp32BaseLog::Level level) {
    switch (level) {
        case Esp32BaseLog::ERROR: return "ERROR";
        case Esp32BaseLog::WARN: return "WARN ";
        case Esp32BaseLog::INFO: return "INFO ";
        case Esp32BaseLog::DEBUG: return "DEBUG";
        case Esp32BaseLog::VERBOSE: return "VERB ";
        default: return "NONE ";
    }
}
}

bool Esp32BaseLog::begin(uint32_t baud) {
    Serial.begin(baud);
    return true;
}

void Esp32BaseLog::setRuntimeLevel(Level level) {
    if (level > static_cast<Level>(ESP32BASE_LOG_LEVEL)) {
        g_runtimeLevel = static_cast<Level>(ESP32BASE_LOG_LEVEL);
        return;
    }
    g_runtimeLevel = level;
}

Esp32BaseLog::Level Esp32BaseLog::runtimeLevel() {
    return g_runtimeLevel;
}

void Esp32BaseLog::setSerialLevel(Level level) {
    if (level > static_cast<Level>(ESP32BASE_LOG_LEVEL)) {
        g_serialLevel = static_cast<Level>(ESP32BASE_LOG_LEVEL);
        return;
    }
    g_serialLevel = level;
}

Esp32BaseLog::Level Esp32BaseLog::serialLevel() {
    return g_serialLevel;
}

void Esp32BaseLog::setSink(Sink sink) {
    g_sink = sink;
}

void Esp32BaseLog::setInternalLineSink(LineSink sink) {
    g_internalLineSink = sink;
}

void Esp32BaseLog::setTimeProvider(TimeProvider provider) {
    g_timeProvider = provider;
}

void Esp32BaseLog::write(Level level, const char* tag, const char* fmt, ...) {
    if (level == NONE || level > g_runtimeLevel || level > static_cast<Level>(ESP32BASE_LOG_LEVEL)) {
        return;
    }

    char message[192];
    va_list args;
    va_start(args, fmt);
    vsnprintf(message, sizeof(message), fmt ? fmt : "", args);
    va_end(args);

    char timestamp[24];
    if (g_timeProvider) {
        strlcpy(timestamp, g_timeProvider(), sizeof(timestamp));
    } else {
        snprintf(timestamp, sizeof(timestamp), "%lu", static_cast<unsigned long>(millis()));
    }

    char line[256];
    snprintf(line, sizeof(line), "[%s] %-5s %-12s %s", timestamp, levelName(level), tag ? tag : "", message);
    if (level <= g_serialLevel) {
        Serial.println(line);
    }

    if (g_internalLineSink) {
        g_internalLineSink(level, tag ? tag : "", message, line);
    }
    if (g_sink) {
        g_sink(level, tag ? tag : "", message);
    }
}

void Esp32BaseLog::formatBytes(uint64_t bytes, char* out, size_t len) {
    if (!out || len == 0) {
        return;
    }

    if (bytes >= 1024ULL * 1024ULL) {
        snprintf(out, len, "%.2f MB", static_cast<double>(bytes) / 1024.0 / 1024.0);
    } else if (bytes >= 1024ULL) {
        snprintf(out, len, "%.2f KB", static_cast<double>(bytes) / 1024.0);
    } else {
        snprintf(out, len, "%llu B", static_cast<unsigned long long>(bytes));
    }
}

void Esp32BaseLog::formatMillis(uint32_t ms, char* out, size_t len) {
    if (!out || len == 0) {
        return;
    }
    snprintf(out, len, "%lu ms", static_cast<unsigned long>(ms));
}

void Esp32BaseLog::formatUptime(uint32_t ms, char* out, size_t len) {
    formatUptime64(ms, out, len);
}

void Esp32BaseLog::formatUptime64(uint64_t ms, char* out, size_t len) {
    if (!out || len == 0) {
        return;
    }

    const uint64_t totalSeconds = ms / 1000ULL;
    const uint32_t seconds = static_cast<uint32_t>(totalSeconds % 60ULL);
    const uint32_t minutes = static_cast<uint32_t>((totalSeconds / 60ULL) % 60ULL);
    const uint32_t hours = static_cast<uint32_t>((totalSeconds / 3600ULL) % 24ULL);
    const uint64_t days = totalSeconds / 86400ULL;
    snprintf(out, len, "%llud %02lu:%02lu:%02lu",
             static_cast<unsigned long long>(days),
             static_cast<unsigned long>(hours),
             static_cast<unsigned long>(minutes),
             static_cast<unsigned long>(seconds));
}
