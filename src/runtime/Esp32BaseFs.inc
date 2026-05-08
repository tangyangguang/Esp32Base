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

bool writeMode(const char* path, const uint8_t* data, size_t len, const char* mode) {
    if (!g_fsReady || !validPath(path) || (!data && len > 0)) {
        return false;
    }
    File file = LittleFS.open(path, mode);
    if (!file) {
        return false;
    }
    const size_t written = len ? file.write(data, len) : 0;
    file.close();
    return written == len;
}
}

bool Esp32BaseFs::begin() {
    g_fsReady = LittleFS.begin(false);
    if (!g_fsReady) {
        ESP32BASE_LOG_W("fs", "LittleFS mount failed");
    } else {
        ESP32BASE_LOG_D("fs", "mounted used=%lu total=%lu",
                        static_cast<unsigned long>(LittleFS.usedBytes()),
                        static_cast<unsigned long>(LittleFS.totalBytes()));
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
    const size_t n = file.read(out, maxLen);
    file.close();
    if (readLen) {
        *readLen = n;
    }
    return true;
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
    const size_t n = maxLen ? file.read(out, maxLen) : 0;
    file.close();
    if (readLen) {
        *readLen = n;
    }
    return true;
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
    file.close();
    return written == len;
}

bool Esp32BaseFs::removeFile(const char* path) {
    return g_fsReady && validPath(path) && LittleFS.remove(path);
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
