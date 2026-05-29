#include "../Esp32BaseProfile.h"

#if ESP32BASE_ENABLE_FS

#include "Esp32BaseFs.h"
#include "../core/Esp32BaseLog.h"

#include <LittleFS.h>

namespace {
bool g_fsReady = false;

bool validPath(const char* path) {
    return path && path[0] == '/';
}

bool readFromOpenFile(File& file, uint8_t* out, size_t maxLen, size_t* readLen) {
    const size_t positionBefore = file.position();
    const size_t size = file.size();
    const size_t n = maxLen ? file.read(out, maxLen) : 0;
    if (readLen) {
        *readLen = n;
    }
    if (maxLen == 0 || n > 0 || positionBefore >= size) {
        return true;
    }
    return !(positionBefore < size && n == 0);
}

bool verifyFileSize(const char* path, uint64_t expectedSize) {
    File verify = LittleFS.open(path, "r");
    if (!verify || verify.isDirectory()) {
        if (verify) {
            verify.close();
        }
        return false;
    }
    const bool ok = static_cast<uint64_t>(verify.size()) == expectedSize;
    verify.close();
    return ok;
}

bool verifyReadableByte(const char* path, uint64_t offset) {
    if (offset > UINT32_MAX) {
        return false;
    }
    uint8_t value = 0;
    size_t readLen = 0;
    return Esp32BaseFs::readBytesAt(path,
                                    static_cast<uint32_t>(offset),
                                    &value,
                                    1,
                                    &readLen) &&
           readLen == 1;
}

bool writeMode(const char* path, const uint8_t* data, size_t len, const char* mode) {
    if (!g_fsReady || !validPath(path) || (!data && len > 0)) {
        return false;
    }
    const bool append = mode && mode[0] == 'a';
    const int64_t before = append ? Esp32BaseFs::fileSize(path) : 0;
    if (append && before > 0 && !verifyReadableByte(path, static_cast<uint64_t>(before - 1))) {
        return false;
    }
    const uint64_t expectedSize = (append && before > 0 ? static_cast<uint64_t>(before) : 0ULL) + len;
    File file = LittleFS.open(path, mode);
    if (!file) {
        return false;
    }
    const size_t written = len ? file.write(data, len) : 0;
    file.flush();
    file.close();
    if (written != len || !verifyFileSize(path, expectedSize)) {
        return false;
    }
    if (expectedSize == 0 || len == 0) {
        return true;
    }
    const uint64_t verifyOffset = expectedSize - 1U;
    return verifyReadableByte(path, verifyOffset);
}
}

bool Esp32BaseFs::begin() {
    g_fsReady = LittleFS.begin(false);
    if (!g_fsReady) {
        ESP32BASE_LOG_W("fs", "LittleFS mount failed");
    } else {
        char used[48];
        char total[48];
        Esp32BaseLog::formatBytes(LittleFS.usedBytes(), used, sizeof(used));
        Esp32BaseLog::formatBytes(LittleFS.totalBytes(), total, sizeof(total));
        ESP32BASE_LOG_D("fs", "mounted used=%s total=%s", used, total);
    }
    return g_fsReady;
}

bool Esp32BaseFs::isReady() {
    return g_fsReady;
}

bool Esp32BaseFs::format() {
    LittleFS.end();
    g_fsReady = false;
    const bool ok = LittleFS.format();
    ESP32BASE_LOG_W("fs", "LittleFS format result=%s", ok ? "success" : "failed");
    return ok;
}

bool Esp32BaseFs::writeFile(const char* path, const char* content) {
    return writeBytes(path, reinterpret_cast<const uint8_t*>(content ? content : ""), content ? strlen(content) : 0);
}

bool Esp32BaseFs::readFile(const char* path, char* out, size_t len) {
    if (!out || len == 0) {
        return false;
    }
    size_t readLen = 0;
    const bool ok = readBytes(path, reinterpret_cast<uint8_t*>(out), len - 1, &readLen);
    out[readLen] = '\0';
    return ok;
}

bool Esp32BaseFs::appendFile(const char* path, const char* content) {
    return appendBytes(path, reinterpret_cast<const uint8_t*>(content ? content : ""), content ? strlen(content) : 0);
}

bool Esp32BaseFs::writeBytes(const char* path, const uint8_t* data, size_t len) {
    return writeMode(path, data, len, "w");
}

bool Esp32BaseFs::readBytes(const char* path, uint8_t* out, size_t maxLen, size_t* readLen) {
    if (readLen) {
        *readLen = 0;
    }
    if (!g_fsReady || !validPath(path) || !out) {
        return false;
    }
    File file = LittleFS.open(path, "r");
    if (!file) {
        return false;
    }
    const bool ok = readFromOpenFile(file, out, maxLen, readLen);
    file.close();
    return ok;
}

bool Esp32BaseFs::appendBytes(const char* path, const uint8_t* data, size_t len) {
    return writeMode(path, data, len, "a");
}

bool Esp32BaseFs::readBytesAt(const char* path, uint32_t offset, uint8_t* out, size_t maxLen, size_t* readLen) {
    if (readLen) {
        *readLen = 0;
    }
    if (!g_fsReady || !validPath(path) || !out) {
        return false;
    }
    File file = LittleFS.open(path, "r");
    if (!file) {
        return false;
    }
    const size_t size = file.size();
    if (offset > size || !file.seek(offset, SeekSet)) {
        file.close();
        return false;
    }
    const bool ok = readFromOpenFile(file, out, maxLen, readLen);
    file.close();
    return ok;
}

bool Esp32BaseFs::writeBytesAt(const char* path, uint32_t offset, const uint8_t* data, size_t len) {
    if (!g_fsReady || !validPath(path) || (!data && len > 0)) {
        return false;
    }
    File file = LittleFS.open(path, "r+");
    if (!file) {
        return false;
    }
    const size_t size = file.size();
    if (offset > size || len > size - offset || !file.seek(offset, SeekSet)) {
        file.close();
        return false;
    }
    const size_t written = len ? file.write(data, len) : 0;
    file.flush();
    file.close();
    if (written != len || !verifyFileSize(path, size)) {
        return false;
    }
    if (len == 0) {
        return true;
    }
    const uint64_t verifyOffset = static_cast<uint64_t>(offset) + len - 1U;
    return verifyReadableByte(path, verifyOffset);
}

bool Esp32BaseFs::removeFile(const char* path) {
    if (!g_fsReady || !validPath(path) || !LittleFS.exists(path)) {
        return false;
    }
    if (LittleFS.remove(path)) {
        return true;
    }
    File file = LittleFS.open(path, "w");
    if (!file) {
        return false;
    }
    file.flush();
    file.close();
    if (LittleFS.remove(path)) {
        return true;
    }
    const int64_t remainingSize = fileSize(path);
    return remainingSize == 0;
}

bool Esp32BaseFs::rename(const char* from, const char* to) {
    return g_fsReady && validPath(from) && validPath(to) && LittleFS.rename(from, to);
}

bool Esp32BaseFs::exists(const char* path) {
    return g_fsReady && validPath(path) && LittleFS.exists(path);
}

int64_t Esp32BaseFs::fileSize(const char* path) {
    if (!g_fsReady || !validPath(path)) {
        return -1;
    }
    File file = LittleFS.open(path, "r");
    if (!file) {
        return -1;
    }
    const int64_t size = file.size();
    file.close();
    return size;
}

bool Esp32BaseFs::listDir(const char* path, ListCallback cb, void* user) {
    if (!g_fsReady || !validPath(path) || !cb) {
        return false;
    }
    File root = LittleFS.open(path);
    if (!root) {
        return false;
    }
    if (!root.isDirectory()) {
        root.close();
        return false;
    }
    File file = root.openNextFile();
    while (file) {
        cb(file.name(), file.size(), file.isDirectory(), user);
        file.close();
        file = root.openNextFile();
    }
    root.close();
    return true;
}

bool Esp32BaseFs::mkdir(const char* path) {
    return g_fsReady && validPath(path) && LittleFS.mkdir(path);
}

bool Esp32BaseFs::rmdir(const char* path) {
    return g_fsReady && validPath(path) && LittleFS.rmdir(path);
}

size_t Esp32BaseFs::totalBytes() {
    return g_fsReady ? LittleFS.totalBytes() : 0;
}

size_t Esp32BaseFs::usedBytes() {
    return g_fsReady ? LittleFS.usedBytes() : 0;
}

size_t Esp32BaseFs::freeBytes() {
    const size_t total = totalBytes();
    const size_t used = usedBytes();
    return total > used ? total - used : 0;
}

#endif
