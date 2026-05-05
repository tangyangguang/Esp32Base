#include "Esp32BaseLog.h"

#include <stdarg.h>
#include <stdio.h>

namespace {
Esp32BaseLog::Level g_runtimeLevel = static_cast<Esp32BaseLog::Level>(ESP32BASE_LOG_LEVEL);
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
    Serial.println(line);

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

    const char* unit = "B";
    double value = static_cast<double>(bytes);
    if (bytes >= 1024ULL * 1024ULL) {
        value = value / 1024.0 / 1024.0;
        unit = "MB";
    } else if (bytes >= 1024ULL) {
        value = value / 1024.0;
        unit = "KB";
    }

    if (bytes < 1024ULL) {
        snprintf(out, len, "%llu bytes (%llu %s)", static_cast<unsigned long long>(bytes), static_cast<unsigned long long>(bytes), unit);
    } else {
        snprintf(out, len, "%llu bytes (%.2f %s)", static_cast<unsigned long long>(bytes), value, unit);
    }
}

void Esp32BaseLog::formatMillis(uint32_t ms, char* out, size_t len) {
    if (!out || len == 0) {
        return;
    }
    snprintf(out, len, "%lu ms", static_cast<unsigned long>(ms));
}

void Esp32BaseLog::formatUptime(uint32_t ms, char* out, size_t len) {
    if (!out || len == 0) {
        return;
    }

    const uint32_t totalSeconds = ms / 1000U;
    const uint32_t seconds = totalSeconds % 60U;
    const uint32_t minutes = (totalSeconds / 60U) % 60U;
    const uint32_t hours = (totalSeconds / 3600U) % 24U;
    const uint32_t days = totalSeconds / 86400U;
    snprintf(out, len, "%lud %02lu:%02lu:%02lu",
             static_cast<unsigned long>(days),
             static_cast<unsigned long>(hours),
             static_cast<unsigned long>(minutes),
             static_cast<unsigned long>(seconds));
}
