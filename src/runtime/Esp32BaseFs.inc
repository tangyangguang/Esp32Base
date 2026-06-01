#include "../Esp32BaseProfile.h"

#if ESP32BASE_ENABLE_FS

#include "Esp32BaseFs.h"
#include "../core/Esp32BaseLog.h"
#include "internal/Esp32BaseLongOperation.h"

#include <LittleFS.h>

#include <string.h>

namespace {
bool g_fsReady = false;
constexpr size_t kFsIoChunkSize = 512;
constexpr size_t kFsServiceEveryBytes = 4096;

bool validPath(const char* path) {
    return path && path[0] == '/';
}

bool readFromOpenFile(File& file, uint8_t* out, size_t maxLen, size_t* readLen) {
    const size_t positionBefore = file.position();
    const size_t size = file.size();
    size_t n = 0;
    if (positionBefore < size && maxLen > 0) {
        const size_t available = size - positionBefore;
        const size_t target = maxLen < available ? maxLen : available;
        size_t serviced = 0;
        while (n < target) {
            const size_t chunkLen = target - n > kFsIoChunkSize ? kFsIoChunkSize : target - n;
            const size_t got = file.read(out + n, chunkLen);
            if (got == 0) {
                break;
            }
            n += got;
            serviced += got;
            if (serviced >= kFsServiceEveryBytes) {
                Esp32BaseLongOperation::service();
                serviced = 0;
            }
        }
    }
    if (readLen) {
        *readLen = n;
    }
    if (maxLen == 0 || n > 0 || positionBefore >= size) {
        return true;
    }
    return !(positionBefore < size && n == 0);
}

bool verifyFileSize(const char* path, uint64_t expectedSize) {
    Esp32BaseLongOperation::LongOperationScope scope;
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

bool verifyByteEquals(const char* path, uint32_t offset, uint8_t expected) {
    uint8_t value = 0;
    size_t readLen = 0;
    return Esp32BaseFs::readBytesAt(path, offset, &value, 1, &readLen) &&
           readLen == 1 &&
           value == expected;
}

bool existingFixedFileUsable(const char* path, uint32_t expectedSize) {
    Esp32BaseLongOperation::LongOperationScope scope;
    File file = LittleFS.open(path, "r");
    if (!file || file.isDirectory()) {
        if (file) {
            file.close();
        }
        return false;
    }
    const bool sizeOk = static_cast<uint64_t>(file.size()) == expectedSize;
    file.close();
    if (!sizeOk) {
        return false;
    }
    return expectedSize == 0 || verifyReadableByte(path, static_cast<uint64_t>(expectedSize - 1U));
}

bool writeOpenFileChunked(File& file, const uint8_t* data, size_t len, size_t* writtenLen = nullptr) {
    size_t written = 0;
    size_t serviced = 0;
    while (written < len) {
        const size_t chunkLen = len - written > kFsIoChunkSize ? kFsIoChunkSize : len - written;
        const size_t n = file.write(data + written, chunkLen);
        if (n == 0) {
            break;
        }
        written += n;
        serviced += n;
        if (serviced >= kFsServiceEveryBytes) {
            Esp32BaseLongOperation::service();
            serviced = 0;
        }
        if (n != chunkLen) {
            break;
        }
    }
    if (writtenLen) {
        *writtenLen = written;
    }
    return written == len;
}

bool readOpenFileChunked(File& file, uint8_t* out, size_t maxLen, size_t* readLen) {
    return readFromOpenFile(file, out, maxLen, readLen);
}

void flushOpenFile(File& file) {
    file.flush();
    Esp32BaseLongOperation::service();
}

bool writeMode(const char* path, const uint8_t* data, size_t len, const char* mode) {
    if (!g_fsReady || !validPath(path) || (!data && len > 0)) {
        return false;
    }
    Esp32BaseLongOperation::LongOperationScope scope;
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
    size_t written = 0;
    const bool writeOk = len == 0 || writeOpenFileChunked(file, data, len, &written);
    flushOpenFile(file);
    file.close();
    if (!writeOk || written != len || !verifyFileSize(path, expectedSize)) {
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
    Esp32BaseLongOperation::LongOperationScope scope;
    Esp32BaseLongOperation::service();
    LittleFS.end();
    g_fsReady = false;
    const bool ok = LittleFS.format();
    Esp32BaseLongOperation::service();
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
    Esp32BaseLongOperation::LongOperationScope scope;
    File file = LittleFS.open(path, "r");
    if (!file) {
        return false;
    }
    const bool ok = readOpenFileChunked(file, out, maxLen, readLen);
    file.close();
    return ok;
}

bool Esp32BaseFs::appendBytes(const char* path, const uint8_t* data, size_t len) {
    return writeMode(path, data, len, "a");
}

bool Esp32BaseFs::createFixedFile(const char* path, uint32_t size, uint8_t fillByte) {
    if (!g_fsReady || !validPath(path)) {
        return false;
    }

    if (existingFixedFileUsable(path, size)) {
        return true;
    }

    Esp32BaseLongOperation::LongOperationScope scope;
    File file = LittleFS.open(path, "w");
    if (!file) {
        return false;
    }

    uint8_t chunk[kFsIoChunkSize];
    memset(chunk, fillByte, sizeof(chunk));
    uint32_t remaining = size;
    size_t serviced = 0;
    bool ok = true;
    while (remaining > 0) {
        const size_t chunkLen = remaining > sizeof(chunk) ? sizeof(chunk) : static_cast<size_t>(remaining);
        if (!writeOpenFileChunked(file, chunk, chunkLen)) {
            ok = false;
            break;
        }
        remaining -= static_cast<uint32_t>(chunkLen);
        serviced += chunkLen;
        if (serviced >= kFsServiceEveryBytes) {
            Esp32BaseLongOperation::service();
            serviced = 0;
        }
    }

    flushOpenFile(file);
    file.close();

    if (!ok || !verifyFileSize(path, size)) {
        return false;
    }
    if (size == 0) {
        return true;
    }

    const uint32_t middle = size / 2U;
    return verifyByteEquals(path, 0, fillByte) &&
           verifyByteEquals(path, middle, fillByte) &&
           verifyByteEquals(path, size - 1U, fillByte);
}

bool Esp32BaseFs::readBytesAt(const char* path, uint32_t offset, uint8_t* out, size_t maxLen, size_t* readLen) {
    if (readLen) {
        *readLen = 0;
    }
    if (!g_fsReady || !validPath(path) || !out) {
        return false;
    }
    Esp32BaseLongOperation::LongOperationScope scope;
    File file = LittleFS.open(path, "r");
    if (!file) {
        return false;
    }
    const size_t size = file.size();
    if (offset > size || !file.seek(offset, SeekSet)) {
        file.close();
        return false;
    }
    const bool ok = readOpenFileChunked(file, out, maxLen, readLen);
    file.close();
    return ok;
}

bool Esp32BaseFs::writeBytesAt(const char* path, uint32_t offset, const uint8_t* data, size_t len) {
    if (!g_fsReady || !validPath(path) || (!data && len > 0)) {
        return false;
    }
    Esp32BaseLongOperation::LongOperationScope scope;
    File file = LittleFS.open(path, "r+");
    if (!file) {
        return false;
    }
    const size_t size = file.size();
    if (offset > size || len > size - offset || !file.seek(offset, SeekSet)) {
        file.close();
        return false;
    }
    size_t written = 0;
    const bool writeOk = len == 0 || writeOpenFileChunked(file, data, len, &written);
    flushOpenFile(file);
    file.close();
    if (!writeOk || written != len || !verifyFileSize(path, size)) {
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
    Esp32BaseLongOperation::LongOperationScope scope;
    Esp32BaseLongOperation::service();
    if (LittleFS.remove(path)) {
        Esp32BaseLongOperation::service();
        return true;
    }
    File file = LittleFS.open(path, "w");
    if (!file) {
        return false;
    }
    flushOpenFile(file);
    file.close();
    Esp32BaseLongOperation::service();
    if (LittleFS.remove(path)) {
        Esp32BaseLongOperation::service();
        return true;
    }
    const int64_t remainingSize = fileSize(path);
    return remainingSize == 0;
}

bool Esp32BaseFs::rename(const char* from, const char* to) {
    if (!g_fsReady || !validPath(from) || !validPath(to)) {
        return false;
    }
    Esp32BaseLongOperation::LongOperationScope scope;
    Esp32BaseLongOperation::service();
    const bool ok = LittleFS.rename(from, to);
    Esp32BaseLongOperation::service();
    return ok;
}

bool Esp32BaseFs::exists(const char* path) {
    return g_fsReady && validPath(path) && LittleFS.exists(path);
}

int64_t Esp32BaseFs::fileSize(const char* path) {
    if (!g_fsReady || !validPath(path)) {
        return -1;
    }
    Esp32BaseLongOperation::LongOperationScope scope;
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
    Esp32BaseLongOperation::LongOperationScope scope;
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
        Esp32BaseLongOperation::service();
        file = root.openNextFile();
    }
    root.close();
    return true;
}

bool Esp32BaseFs::mkdir(const char* path) {
    if (!g_fsReady || !validPath(path)) {
        return false;
    }
    Esp32BaseLongOperation::LongOperationScope scope;
    Esp32BaseLongOperation::service();
    const bool ok = LittleFS.mkdir(path);
    Esp32BaseLongOperation::service();
    return ok;
}

bool Esp32BaseFs::rmdir(const char* path) {
    if (!g_fsReady || !validPath(path)) {
        return false;
    }
    Esp32BaseLongOperation::LongOperationScope scope;
    Esp32BaseLongOperation::service();
    const bool ok = LittleFS.rmdir(path);
    Esp32BaseLongOperation::service();
    return ok;
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
