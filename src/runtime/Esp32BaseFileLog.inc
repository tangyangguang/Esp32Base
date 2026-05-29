#include "../Esp32BaseProfile.h"

#if ESP32BASE_ENABLE_FILELOG

#include "Esp32BaseFileLog.h"
#include "Esp32BaseFs.h"
#include "../core/Esp32BaseConfig.h"
#include "../core/Esp32BaseLog.h"
#if ESP32BASE_ENABLE_WATCHDOG
#include "Esp32BaseWatchdog.h"
#endif

#include <LittleFS.h>

#include <string.h>

namespace {
bool g_fileLogEnabled = false;
bool g_fileLogDirReady = false;
bool g_fileLogFault = false;
uint32_t g_fileLogMaxBytes = ESP32BASE_EB_FILELOG_MAX_BYTES;
uint32_t g_fileLogCurrentBytes = 0;
uint8_t g_fileLogRotateFiles = ESP32BASE_EB_FILELOG_ROTATE_FILES;
Esp32BaseFileLog::Mode g_fileLogMode = static_cast<Esp32BaseFileLog::Mode>(ESP32BASE_EB_FILELOG_DEFAULT_MODE);
char g_fileLogPath[48] = ESP32BASE_EB_FILELOG_PATH;

#if ESP32BASE_EB_FILELOG_BUFFER_SIZE > 0
char g_fileLogBuffer[ESP32BASE_EB_FILELOG_BUFFER_SIZE];
uint16_t g_fileLogBufferUsed = 0;
uint32_t g_fileLogLastFlushMs = 0;
#endif

const char* modeName(Esp32BaseFileLog::Mode mode) {
    switch (mode) {
        case Esp32BaseFileLog::ERROR: return "ERROR";
        case Esp32BaseFileLog::WARN: return "WARN";
        case Esp32BaseFileLog::INFO: return "INFO";
        case Esp32BaseFileLog::OFF:
        default: return "OFF";
    }
}

bool validMode(Esp32BaseFileLog::Mode mode) {
    if (mode != Esp32BaseFileLog::OFF && mode != Esp32BaseFileLog::ERROR && mode != Esp32BaseFileLog::WARN && mode != Esp32BaseFileLog::INFO) {
        return false;
    }
    return static_cast<uint8_t>(mode) <= ESP32BASE_LOG_LEVEL;
}

Esp32BaseFileLog::Mode readMode() {
    const int32_t raw = Esp32BaseConfig::getInt("eb_log", "mode", ESP32BASE_EB_FILELOG_DEFAULT_MODE);
    const Esp32BaseFileLog::Mode mode = static_cast<Esp32BaseFileLog::Mode>(raw);
    return validMode(mode) ? mode : static_cast<Esp32BaseFileLog::Mode>(ESP32BASE_EB_FILELOG_DEFAULT_MODE);
}

Esp32BaseLog::Level modeLevel(Esp32BaseFileLog::Mode mode) {
    switch (mode) {
        case Esp32BaseFileLog::ERROR: return Esp32BaseLog::ERROR;
        case Esp32BaseFileLog::WARN: return Esp32BaseLog::WARN;
        case Esp32BaseFileLog::INFO: return Esp32BaseLog::INFO;
        case Esp32BaseFileLog::OFF:
        default: return Esp32BaseLog::NONE;
    }
}

bool beginLongFsOperation();
void endLongFsOperation(bool active);

void markFileLogFault() {
    g_fileLogFault = true;
    g_fileLogEnabled = false;
    g_fileLogDirReady = false;
    g_fileLogCurrentBytes = 0;
#if ESP32BASE_EB_FILELOG_BUFFER_SIZE > 0
    g_fileLogBufferUsed = 0;
    g_fileLogLastFlushMs = 0;
#endif
}

bool truncateSegment(uint8_t index) {
    char seg[64];
    if (!Esp32BaseFileLog::segmentPath(index, seg, sizeof(seg))) {
        return false;
    }
    const bool watchdogReleased = beginLongFsOperation();
    const bool ok = Esp32BaseFs::writeBytes(seg, nullptr, 0);
    endLongFsOperation(watchdogReleased);
    return ok;
}

bool ensureSegmentFiles() {
    const bool watchdogReleased = beginLongFsOperation();
    bool ok = true;
    for (uint8_t i = 0; i < g_fileLogRotateFiles; ++i) {
        char seg[64];
        if (!Esp32BaseFileLog::segmentPath(i, seg, sizeof(seg))) {
            ok = false;
            break;
        }
        File file = LittleFS.open(seg, "a");
        if (!file) {
            ok = false;
            break;
        }
        file.close();
    }
    endLongFsOperation(watchdogReleased);
    return ok;
}

bool ensureDirReady() {
    if (g_fileLogDirReady) {
        return true;
    }
    if (!Esp32BaseFs::isReady() || !g_fileLogPath[0]) {
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
        const bool watchdogReleased = beginLongFsOperation();
        const bool dirReady = Esp32BaseFs::mkdir(dir) || Esp32BaseFs::exists(dir);
        endLongFsOperation(watchdogReleased);
        if (!dirReady) {
            markFileLogFault();
            ESP32BASE_LOG_W("filelog", "dir_create_failed path=%s", dir);
            return false;
        }
    }
    if (!ensureSegmentFiles()) {
        markFileLogFault();
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
    const bool watchdogReleased = beginLongFsOperation();
    const bool ok = Esp32BaseFs::writeBytes(g_fileLogPath, nullptr, 0);
    endLongFsOperation(watchdogReleased);
    if (ok) {
        g_fileLogCurrentBytes = 0;
    }
    return ok;
}

bool beginLongFsOperation() {
#if ESP32BASE_ENABLE_WATCHDOG
    if (Esp32BaseWatchdog::currentTaskRemovedForLongOperation()) {
        return false;
    }
    return Esp32BaseWatchdog::removeCurrentTaskForLongOperation();
#else
    return true;
#endif
}

void endLongFsOperation(bool active) {
#if ESP32BASE_ENABLE_WATCHDOG
    if (active) {
        Esp32BaseWatchdog::restoreCurrentTaskAfterLongOperation();
    }
#else
    (void)active;
#endif
}

bool appendCurrentChunk(const char* data, size_t len) {
    const bool watchdogReleased = beginLongFsOperation();
    const bool ok = Esp32BaseFs::appendBytes(g_fileLogPath,
                                             reinterpret_cast<const uint8_t*>(data),
                                             len);
    endLongFsOperation(watchdogReleased);
    return ok;
}

bool rotateFile() {
    const bool watchdogReleased = beginLongFsOperation();
    bool ok = true;
    if (g_fileLogRotateFiles <= 1) {
        ok = truncateCurrent();
        endLongFsOperation(watchdogReleased);
        return ok;
    }

    char oldest[64];
    if (Esp32BaseFileLog::segmentPath(g_fileLogRotateFiles - 1, oldest, sizeof(oldest)) && Esp32BaseFs::exists(oldest)) {
        if (!Esp32BaseFs::removeFile(oldest)) {
            ok = false;
        } else {
            yield();
        }
    }

    for (int8_t i = static_cast<int8_t>(g_fileLogRotateFiles) - 2; ok && i >= 0; --i) {
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
                ok = false;
                break;
            }
            yield();
            if (!Esp32BaseFs::rename(from, to)) {
                ok = false;
                break;
            }
            yield();
        }
    }

    if (ok) {
        ok = truncateCurrent();
    }
    endLongFsOperation(watchdogReleased);
    return ok;
}

bool writeBytes(const char* data, size_t len) {
    if (!g_fileLogEnabled || !g_fileLogPath[0] || !g_fileLogMaxBytes || (!data && len > 0)) {
        return false;
    }
    if (!ensureDirReady()) {
        markFileLogFault();
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
        if (!appendCurrentChunk(data + offset, chunk)) {
            if (!truncateCurrent()) {
                markFileLogFault();
                return false;
            }
            if (!appendCurrentChunk(data + offset, chunk)) {
                markFileLogFault();
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
    const Esp32BaseLog::Level fileLevel = modeLevel(g_fileLogMode);
    const bool shouldWrite = level <= fileLevel && level != Esp32BaseLog::NONE;
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

bool applyMode(Esp32BaseFileLog::Mode mode, bool persist, bool logChange) {
    if (!validMode(mode)) {
        return false;
    }
    const Esp32BaseFileLog::Mode previousMode = g_fileLogMode;
    if (mode == Esp32BaseFileLog::OFF) {
        Esp32BaseFileLog::flush();
        g_fileLogFault = false;
        g_fileLogEnabled = false;
        g_fileLogMode = Esp32BaseFileLog::OFF;
        g_fileLogDirReady = false;
        g_fileLogCurrentBytes = 0;
#if ESP32BASE_EB_FILELOG_BUFFER_SIZE > 0
        g_fileLogBufferUsed = 0;
        g_fileLogLastFlushMs = 0;
#endif
        const bool saved = persist ? Esp32BaseConfig::setInt("eb_log", "mode", static_cast<int32_t>(g_fileLogMode)) : true;
        if (logChange) {
            if (previousMode == g_fileLogMode) {
                ESP32BASE_LOG_D("filelog", "mode_unchanged mode=%s saved=%s",
                                ::modeName(g_fileLogMode),
                                saved ? "success" : "failed");
            } else {
                ESP32BASE_LOG_W("filelog", "mode_changed from=%s to=%s saved=%s",
                                ::modeName(previousMode),
                                ::modeName(g_fileLogMode),
                                saved ? "success" : "failed");
            }
        }
        return saved;
    }
    if (!Esp32BaseFs::isReady() || !g_fileLogPath[0] || g_fileLogPath[0] != '/' || g_fileLogMaxBytes < 256) {
        return false;
    }
    if (g_fileLogRotateFiles < 1) {
        g_fileLogRotateFiles = 1;
    }
    if (g_fileLogRotateFiles > 4) {
        g_fileLogRotateFiles = 4;
    }
    if (g_fileLogEnabled) {
        Esp32BaseFileLog::flush();
    }
    g_fileLogMode = mode;
    g_fileLogFault = false;
    const Esp32BaseLog::Level fileLevel = modeLevel(g_fileLogMode);
    if (Esp32BaseLog::runtimeLevel() < fileLevel) {
        Esp32BaseLog::setRuntimeLevel(fileLevel);
    }
    g_fileLogDirReady = false;
    g_fileLogCurrentBytes = 0;
#if ESP32BASE_EB_FILELOG_BUFFER_SIZE > 0
    g_fileLogBufferUsed = 0;
    g_fileLogLastFlushMs = 0;
#endif
    g_fileLogEnabled = true;
    const bool saved = persist ? Esp32BaseConfig::setInt("eb_log", "mode", static_cast<int32_t>(g_fileLogMode)) : true;
    if (logChange) {
        char maxBytes[48];
        char bufferBytes[48];
        Esp32BaseLog::formatBytes(g_fileLogMaxBytes, maxBytes, sizeof(maxBytes));
        Esp32BaseLog::formatBytes(Esp32BaseFileLog::bufferSize(), bufferBytes, sizeof(bufferBytes));
        if (previousMode == g_fileLogMode) {
            ESP32BASE_LOG_D("filelog", "mode_unchanged mode=%s saved=%s path=%s max=%s rotate=%u buffer=%s flushMs=%lu",
                            ::modeName(g_fileLogMode),
                            saved ? "success" : "failed",
                            g_fileLogPath,
                            maxBytes,
                            static_cast<unsigned>(g_fileLogRotateFiles),
                            bufferBytes,
                            static_cast<unsigned long>(Esp32BaseFileLog::flushIntervalMs()));
        } else {
            ESP32BASE_LOG_W("filelog", "mode_changed from=%s to=%s saved=%s path=%s max=%s rotate=%u buffer=%s flushMs=%lu",
                            ::modeName(previousMode),
                            ::modeName(g_fileLogMode),
                            saved ? "success" : "failed",
                            g_fileLogPath,
                            maxBytes,
                            static_cast<unsigned>(g_fileLogRotateFiles),
                            bufferBytes,
                            static_cast<unsigned long>(Esp32BaseFileLog::flushIntervalMs()));
        }
    }
    return saved;
}
}

bool Esp32BaseFileLog::begin() {
    Esp32BaseLog::setInternalLineSink(logLineSink);
    strlcpy(g_fileLogPath, ESP32BASE_EB_FILELOG_PATH, sizeof(g_fileLogPath));
    g_fileLogMaxBytes = ESP32BASE_EB_FILELOG_MAX_BYTES;
    g_fileLogRotateFiles = ESP32BASE_EB_FILELOG_ROTATE_FILES;
    return applyMode(readMode(), false, false);
}

bool Esp32BaseFileLog::setMode(Mode mode) {
    return applyMode(mode, true, true);
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
    g_fileLogFault = false;
    g_fileLogEnabled = g_fileLogMode != Esp32BaseFileLog::OFF;
    g_fileLogDirReady = true;
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
bool Esp32BaseFileLog::faulted() { return g_fileLogFault; }
Esp32BaseFileLog::Mode Esp32BaseFileLog::mode() { return g_fileLogMode; }
const char* Esp32BaseFileLog::modeName() { return ::modeName(g_fileLogMode); }
const char* Esp32BaseFileLog::path() { return g_fileLogPath; }
uint32_t Esp32BaseFileLog::maxBytes() { return g_fileLogMaxBytes; }
uint8_t Esp32BaseFileLog::rotateFiles() { return g_fileLogRotateFiles; }
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
    return g_fileLogEnabled && g_fileLogMode == Esp32BaseFileLog::INFO;
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
