#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

class Esp32BaseFs {
public:
    static bool begin();
    static bool isReady();
    static bool format();

    static bool writeFile(const char* path, const char* content);
    static bool readFile(const char* path, char* out, size_t len);
    static bool appendFile(const char* path, const char* content);

    static bool writeBytes(const char* path, const uint8_t* data, size_t len);
    static bool readBytes(const char* path, uint8_t* out, size_t maxLen, size_t* readLen);
    static bool appendBytes(const char* path, const uint8_t* data, size_t len);
    static bool readBytesAt(const char* path, uint32_t offset, uint8_t* out, size_t maxLen, size_t* readLen);
    static bool writeBytesAt(const char* path, uint32_t offset, const uint8_t* data, size_t len);

    static bool removeFile(const char* path);
    static bool rename(const char* from, const char* to);
    static bool exists(const char* path);
    static int64_t fileSize(const char* path);

    using ListCallback = void (*)(const char* name, size_t size, bool isDir, void* user);
    static bool listDir(const char* path, ListCallback cb, void* user = nullptr);
    static bool mkdir(const char* path);
    static bool rmdir(const char* path);

    static size_t totalBytes();
    static size_t usedBytes();
    static size_t freeBytes();
};
