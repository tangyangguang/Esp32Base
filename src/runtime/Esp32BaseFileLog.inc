#include "../Esp32BaseProfile.h"

#if ESP32BASE_ENABLE_FILELOG

#include "Esp32BaseFileLog.h"
#include "Esp32BaseFs.h"
#include "../core/Esp32BaseConfig.h"
#include "../core/Esp32BaseLog.h"

#include <LittleFS.h>

#include <string.h>

namespace {
bool g_fileLogEnabled = false;
bool g_fileLogDirReady = false;
uint32_t g_fileLogMaxBytes = ESP32BASE_EB_FILELOG_MAX_BYTES;
uint32_t g_fileLogCurrentBytes = 0;
uint8_t g_fileLogRotateFiles = ESP32BASE_EB_FILELOG_ROTATE_FILES;
Esp32BaseLog::Level g_fileLogLevel = static_cast<Esp32BaseLog::Level>(ESP32BASE_EB_FILELOG_LEVEL);
char g_fileLogPath[48] = ESP32BASE_EB_FILELOG_PATH;

#if ESP32BASE_EB_FILELOG_BUFFER_SIZE > 0
char g_fileLogBuffer[ESP32BASE_EB_FILELOG_BUFFER_SIZE];
uint16_t g_fileLogBufferUsed = 0;
uint32_t g_fileLogLastFlushMs = 0;
#endif

const char* fileLogLevelName(Esp32BaseLog::Level level) {
    switch (level) {
        case Esp32BaseLog::ERROR: return "ERROR";
        case Esp32BaseLog::WARN: return "WARN";
        case Esp32BaseLog::INFO: return "INFO";
        case Esp32BaseLog::DEBUG: return "DEBUG";
        case Esp32BaseLog::VERBOSE: return "VERBOSE";
        case Esp32BaseLog::NONE:
        default: return "NONE";
    }
}

Esp32BaseLog::Level clampFileLogLevel(int32_t value) {
    if (value < Esp32BaseLog::NONE) {
        return Esp32BaseLog::NONE;
    }
    if (value > Esp32BaseLog::VERBOSE) {
        return Esp32BaseLog::VERBOSE;
    }
    return static_cast<Esp32BaseLog::Level>(value);
}

bool truncateSegment(uint8_t index) {
    char seg[64];
    if (!Esp32BaseFileLog::segmentPath(index, seg, sizeof(seg))) {
        return false;
    }
    return Esp32BaseFs::writeBytes(seg, nullptr, 0);
}

bool ensureSegmentFiles() {
    for (uint8_t i = 0; i < g_fileLogRotateFiles; ++i) {
        char seg[64];
        if (!Esp32BaseFileLog::segmentPath(i, seg, sizeof(seg))) {
            return false;
        }
        File file = LittleFS.open(seg, "a");
        if (!file) {
            return false;
        }
        file.close();
    }
    return true;
}

bool ensureDirReady() {
    if (g_fileLogDirReady) {
        return true;
    }
    if (!g_fileLogEnabled || !Esp32BaseFs::isReady() || !g_fileLogPath[0]) {
        return false;
    }
    const char* slash = strrchr(g_fileLogPath, '/');
    if (slash && slash != g_fileLogPath) {
        char dir[48];
        const size_t len = static_cast<size_t>(slash - g_fileLogPath);
        if (len >= sizeof(dir)) {
            return false;
        }
        memcpy(dir, g_fileLogPath, len);
        dir[len] = '\0';
        if (!Esp32BaseFs::mkdir(dir) && !Esp32BaseFs::exists(dir)) {
            ESP32BASE_LOG_W("filelog", "dir_create_failed path=%s", dir);
            return false;
        }
    }
    if (!ensureSegmentFiles()) {
        ESP32BASE_LOG_W("filelog", "segment_prepare_failed path=%s rotate=%u",
                        g_fileLogPath,
                        static_cast<unsigned>(g_fileLogRotateFiles));
        return false;
    }
    const int64_t current = Esp32BaseFs::fileSize(g_fileLogPath);
    g_fileLogCurrentBytes = current > 0 ? static_cast<uint32_t>(current) : 0;
    g_fileLogDirReady = true;
    return true;
}

bool truncateCurrent() {
    if (!g_fileLogEnabled || !g_fileLogPath[0]) {
        return false;
    }
    const bool ok = Esp32BaseFs::writeBytes(g_fileLogPath, nullptr, 0);
    if (ok) {
        g_fileLogCurrentBytes = 0;
    }
    return ok;
}

bool rotateFile() {
    if (g_fileLogRotateFiles <= 1) {
        return truncateCurrent();
    }

    char oldest[64];
    if (Esp32BaseFileLog::segmentPath(g_fileLogRotateFiles - 1, oldest, sizeof(oldest)) && Esp32BaseFs::exists(oldest)) {
        if (!Esp32BaseFs::removeFile(oldest)) {
            return false;
        }
    }

    for (int8_t i = static_cast<int8_t>(g_fileLogRotateFiles) - 2; i >= 0; --i) {
        char from[64];
        char to[64];
        if (!Esp32BaseFileLog::segmentPath(static_cast<uint8_t>(i), from, sizeof(from))) {
            continue;
        }
        if (!Esp32BaseFileLog::segmentPath(static_cast<uint8_t>(i + 1), to, sizeof(to))) {
            continue;
        }
        if (Esp32BaseFs::exists(from)) {
            if (Esp32BaseFs::exists(to) && !Esp32BaseFs::removeFile(to)) {
                return false;
            }
            if (!Esp32BaseFs::rename(from, to)) {
                return false;
            }
        }
    }

    return truncateCurrent();
}

bool writeBytes(const char* data, size_t len) {
    if (!g_fileLogEnabled || !g_fileLogPath[0] || !g_fileLogMaxBytes || (!data && len > 0)) {
        return false;
    }
    if (!ensureDirReady()) {
        return false;
    }

    size_t offset = 0;
    while (offset < len) {
        if (g_fileLogCurrentBytes >= g_fileLogMaxBytes) {
            if (!rotateFile() && !truncateCurrent()) {
                return false;
            }
        }

        const uint32_t room = g_fileLogMaxBytes - g_fileLogCurrentBytes;
        size_t chunk = len - offset;
        if (chunk > room) {
            chunk = room;
        }
        if (chunk == 0) {
            continue;
        }
        if (!Esp32BaseFs::appendBytes(g_fileLogPath, reinterpret_cast<const uint8_t*>(data + offset), chunk)) {
            if (!truncateCurrent()) {
                return false;
            }
            if (!Esp32BaseFs::appendBytes(g_fileLogPath, reinterpret_cast<const uint8_t*>(data + offset), chunk)) {
                return false;
            }
        }
        g_fileLogCurrentBytes += static_cast<uint32_t>(chunk);
        offset += chunk;
        yield();
    }
    return true;
}

bool writeLineNow(const char* line) {
    if (!line) {
        return false;
    }
    if (!ensureDirReady()) {
        return false;
    }
    const size_t len = strlen(line);
    if (g_fileLogCurrentBytes + len + 1 > g_fileLogMaxBytes) {
        if (!rotateFile() && !truncateCurrent()) {
            return false;
        }
    }
    if (!writeBytes(line, len)) {
        return false;
    }
    const char nl = '\n';
    return writeBytes(&nl, 1);
}

bool writeLineBuffered(const char* line) {
#if ESP32BASE_EB_FILELOG_BUFFER_SIZE > 0
    if (!line) {
        return false;
    }
    if (!Esp32BaseFileLog::bufferEnabled()) {
        return writeLineNow(line);
    }

    const uint32_t now = millis();
    if (g_fileLogBufferUsed > 0 &&
        static_cast<uint32_t>(now - g_fileLogLastFlushMs) >= ESP32BASE_EB_FILELOG_FLUSH_INTERVAL_MS) {
        Esp32BaseFileLog::flush();
    }

    const size_t lineLen = strlen(line);
    const size_t needed = lineLen + 1;
    if (needed > ESP32BASE_EB_FILELOG_BUFFER_SIZE) {
        Esp32BaseFileLog::flush();
        return writeLineNow(line);
    }
    if (g_fileLogBufferUsed + needed > ESP32BASE_EB_FILELOG_BUFFER_SIZE) {
        Esp32BaseFileLog::flush();
    }
    if (g_fileLogBufferUsed == 0) {
        g_fileLogLastFlushMs = now;
    }
    memcpy(g_fileLogBuffer + g_fileLogBufferUsed, line, lineLen);
    g_fileLogBufferUsed = static_cast<uint16_t>(g_fileLogBufferUsed + lineLen);
    g_fileLogBuffer[g_fileLogBufferUsed++] = '\n';

    if (g_fileLogBufferUsed >= ESP32BASE_EB_FILELOG_BUFFER_SIZE ||
        static_cast<uint32_t>(millis() - g_fileLogLastFlushMs) >= ESP32BASE_EB_FILELOG_FLUSH_INTERVAL_MS) {
        return Esp32BaseFileLog::flush();
    }
    return true;
#else
    return writeLineNow(line);
#endif
}

void logLineSink(Esp32BaseLog::Level level, const char*, const char*, const char* line) {
    if (!g_fileLogEnabled || !line) {
        return;
    }
    const bool shouldWrite = level <= g_fileLogLevel && level != Esp32BaseLog::NONE;
    if (!shouldWrite) {
        return;
    }
    if (level <= Esp32BaseLog::WARN) {
        Esp32BaseFileLog::flush();
        writeLineNow(line);
    } else {
        writeLineBuffered(line);
    }
}
}

bool Esp32BaseFileLog::begin() {
    Esp32BaseLog::setInternalLineSink(logLineSink);
#if ESP32BASE_EB_FILELOG_ENABLED
    char configuredPath[sizeof(g_fileLogPath)];
    Esp32BaseConfig::getStr("eb_log", "path", configuredPath, sizeof(configuredPath), ESP32BASE_EB_FILELOG_PATH);
    const bool configuredEnabled = Esp32BaseConfig::getBool("eb_log", "en", true);
    const int32_t configuredMaxRaw = Esp32BaseConfig::getInt("eb_log", "max", ESP32BASE_EB_FILELOG_MAX_BYTES);
    const uint32_t configuredMax = configuredMaxRaw >= 256 ? static_cast<uint32_t>(configuredMaxRaw) : ESP32BASE_EB_FILELOG_MAX_BYTES;
    const int32_t configuredFilesRaw = Esp32BaseConfig::getInt("eb_log", "files", ESP32BASE_EB_FILELOG_ROTATE_FILES);
    const uint8_t configuredFiles = configuredFilesRaw >= 1 ? static_cast<uint8_t>(configuredFilesRaw) : ESP32BASE_EB_FILELOG_ROTATE_FILES;
    const Esp32BaseLog::Level configuredLevel = clampFileLogLevel(Esp32BaseConfig::getInt("eb_log", "level", ESP32BASE_EB_FILELOG_LEVEL));
    if (!configuredEnabled) {
        g_fileLogEnabled = false;
        return true;
    }
    return enable(configuredPath, configuredMax, configuredLevel, configuredFiles);
#else
    return true;
#endif
}

bool Esp32BaseFileLog::enable(const char* pathValue, uint32_t maxBytesValue, Esp32BaseLog::Level fileLevelValue, uint8_t rotateFilesValue) {
    if (!Esp32BaseFs::isReady() || !pathValue || pathValue[0] != '/' || strlen(pathValue) >= sizeof(g_fileLogPath) || maxBytesValue < 256) {
        return false;
    }
    if (g_fileLogEnabled) {
        flush();
    }
    if (rotateFilesValue < 1) {
        rotateFilesValue = 1;
    }
    if (rotateFilesValue > 4) {
        rotateFilesValue = 4;
    }
    strlcpy(g_fileLogPath, pathValue, sizeof(g_fileLogPath));
    g_fileLogMaxBytes = maxBytesValue;
    g_fileLogRotateFiles = rotateFilesValue;
    g_fileLogLevel = fileLevelValue;
    g_fileLogDirReady = false;
    g_fileLogCurrentBytes = 0;
#if ESP32BASE_EB_FILELOG_BUFFER_SIZE > 0
    g_fileLogBufferUsed = 0;
    g_fileLogLastFlushMs = 0;
#endif
    g_fileLogEnabled = true;
    Esp32BaseConfig::setBool("eb_log", "en", true);
    Esp32BaseConfig::setStr("eb_log", "path", g_fileLogPath);
    Esp32BaseConfig::setInt("eb_log", "max", static_cast<int32_t>(g_fileLogMaxBytes));
    Esp32BaseConfig::setInt("eb_log", "files", static_cast<int32_t>(g_fileLogRotateFiles));
    Esp32BaseConfig::setInt("eb_log", "level", static_cast<int32_t>(g_fileLogLevel));
    ESP32BASE_LOG_I("filelog", "enabled path=%s max=%lu rotate=%u level=%s buffer=%u flushMs=%lu",
                    g_fileLogPath,
                    static_cast<unsigned long>(g_fileLogMaxBytes),
                    static_cast<unsigned>(g_fileLogRotateFiles),
                    fileLogLevelName(g_fileLogLevel),
                    static_cast<unsigned>(bufferSize()),
                    static_cast<unsigned long>(flushIntervalMs()));
    return true;
}

void Esp32BaseFileLog::disable() {
    flush();
    g_fileLogEnabled = false;
    g_fileLogDirReady = false;
    g_fileLogCurrentBytes = 0;
    Esp32BaseConfig::setBool("eb_log", "en", false);
#if ESP32BASE_EB_FILELOG_BUFFER_SIZE > 0
    g_fileLogBufferUsed = 0;
    g_fileLogLastFlushMs = 0;
#endif
}

bool Esp32BaseFileLog::flush() {
#if ESP32BASE_EB_FILELOG_BUFFER_SIZE > 0
    if (g_fileLogBufferUsed == 0) {
        return true;
    }
    const bool ok = writeBytes(g_fileLogBuffer, g_fileLogBufferUsed);
    if (ok) {
        g_fileLogBufferUsed = 0;
        g_fileLogLastFlushMs = 0;
    }
    return ok;
#else
    return true;
#endif
}

bool Esp32BaseFileLog::clear() {
    if (!g_fileLogPath[0] || !Esp32BaseFs::isReady()) {
        return false;
    }
#if ESP32BASE_EB_FILELOG_BUFFER_SIZE > 0
    g_fileLogBufferUsed = 0;
    g_fileLogLastFlushMs = 0;
#endif
    if (!ensureDirReady()) {
        return false;
    }
    for (uint8_t i = 0; i < g_fileLogRotateFiles; ++i) {
        if (!truncateSegment(i)) {
            return false;
        }
    }
    g_fileLogCurrentBytes = 0;
    ESP32BASE_LOG_I("filelog", "cleared path=%s", g_fileLogPath);
    return true;
}

void Esp32BaseFileLog::handle() {
#if ESP32BASE_EB_FILELOG_BUFFER_SIZE > 0
    if (g_fileLogBufferUsed > 0 &&
        static_cast<uint32_t>(millis() - g_fileLogLastFlushMs) >= ESP32BASE_EB_FILELOG_FLUSH_INTERVAL_MS) {
        flush();
    }
#endif
}

bool Esp32BaseFileLog::isEnabled() { return g_fileLogEnabled; }
const char* Esp32BaseFileLog::path() { return g_fileLogPath; }
uint32_t Esp32BaseFileLog::maxBytes() { return g_fileLogMaxBytes; }
uint8_t Esp32BaseFileLog::rotateFiles() { return g_fileLogRotateFiles; }
Esp32BaseLog::Level Esp32BaseFileLog::level() { return g_fileLogLevel; }
const char* Esp32BaseFileLog::levelName() { return ::fileLogLevelName(g_fileLogLevel); }
uint32_t Esp32BaseFileLog::size() { return segmentSize(0); }

uint32_t Esp32BaseFileLog::segmentSize(uint8_t index) {
    char seg[64];
    if (!segmentPath(index, seg, sizeof(seg))) {
        return 0;
    }
    const int64_t value = Esp32BaseFs::fileSize(seg);
    return value > 0 ? static_cast<uint32_t>(value) : 0;
}

bool Esp32BaseFileLog::bufferEnabled() {
#if ESP32BASE_EB_FILELOG_BUFFER_SIZE > 0
    return g_fileLogEnabled && g_fileLogLevel >= Esp32BaseLog::INFO;
#else
    return false;
#endif
}

uint16_t Esp32BaseFileLog::bufferSize() {
#if ESP32BASE_EB_FILELOG_BUFFER_SIZE > 0
    return ESP32BASE_EB_FILELOG_BUFFER_SIZE;
#else
    return 0;
#endif
}

uint16_t Esp32BaseFileLog::bufferUsed() {
#if ESP32BASE_EB_FILELOG_BUFFER_SIZE > 0
    return g_fileLogBufferUsed;
#else
    return 0;
#endif
}

uint32_t Esp32BaseFileLog::flushIntervalMs() {
#if ESP32BASE_EB_FILELOG_BUFFER_SIZE > 0
    return ESP32BASE_EB_FILELOG_FLUSH_INTERVAL_MS;
#else
    return 0;
#endif
}

bool Esp32BaseFileLog::segmentPath(uint8_t index, char* out, size_t len) {
    if (!out || len == 0 || !g_fileLogPath[0]) {
        return false;
    }
    const int written = index == 0
        ? snprintf(out, len, "%s", g_fileLogPath)
        : snprintf(out, len, "%s.%u", g_fileLogPath, static_cast<unsigned>(index));
    return written > 0 && static_cast<size_t>(written) < len;
}

bool Esp32BaseFileLog::streamSegment(uint8_t index, SegmentChunkFn cb, void* user) {
    if (!cb || !Esp32BaseFs::isReady()) {
        return false;
    }
    char seg[64];
    if (!segmentPath(index, seg, sizeof(seg)) || !Esp32BaseFs::exists(seg)) {
        return false;
    }
    File file = LittleFS.open(seg, "r");
    if (!file) {
        return false;
    }
    char buf[128];
    while (file.available()) {
        const size_t n = file.readBytes(buf, sizeof(buf));
        if (n > 0) {
            cb(buf, n, user);
        }
        yield();
    }
    file.close();
    return true;
}

#endif
