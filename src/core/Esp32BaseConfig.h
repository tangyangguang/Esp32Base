#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <type_traits>

#ifndef ESP32BASE_EB_CONFIG_PENDING_MAX
#define ESP32BASE_EB_CONFIG_PENDING_MAX 8
#endif

#ifndef ESP32BASE_CONFIG_PENDING_MAX
#define ESP32BASE_CONFIG_PENDING_MAX ESP32BASE_EB_CONFIG_PENDING_MAX
#endif

class Esp32BaseConfig {
public:
    static constexpr size_t CONFIG_BLOB_MAX_LEN = 256;

    static bool begin();
    static void handle();
    static bool isReady();

    static bool setStr(const char* ns, const char* key, const char* value);
    static bool getStr(const char* ns, const char* key, char* out, size_t len, const char* def = "");

    static bool setInt(const char* ns, const char* key, int32_t value);
    static int32_t getInt(const char* ns, const char* key, int32_t def = 0);
    static bool setIntDeferred(const char* ns, const char* key, int32_t value, uint32_t delayMs = 1000);

    static bool setBool(const char* ns, const char* key, bool value);
    static bool getBool(const char* ns, const char* key, bool def = false);
    static bool setBoolDeferred(const char* ns, const char* key, bool value, uint32_t delayMs = 1000);

    static bool setStrDeferred(const char* ns, const char* key, const char* value, uint32_t delayMs = 1000);

    static bool setBlob(const char* ns, const char* key, const void* data, size_t len);
    static bool getBlob(const char* ns, const char* key, void* out, size_t len);
    static bool setBlobDeferred(const char* ns, const char* key, const void* data, size_t len, uint32_t delayMs = 1000);

    template <typename T>
    static bool setPod(const char* ns, const char* key, const T& value) {
        static_assert(std::is_trivially_copyable<T>::value, "Esp32BaseConfig::setPod requires a trivially copyable type");
        static_assert(std::is_standard_layout<T>::value, "Esp32BaseConfig::setPod requires a standard-layout type");
        return setBlob(ns, key, &value, sizeof(T));
    }

    template <typename T>
    static bool getPod(const char* ns, const char* key, T& out, const T& def = T()) {
        static_assert(std::is_trivially_copyable<T>::value, "Esp32BaseConfig::getPod requires a trivially copyable type");
        static_assert(std::is_standard_layout<T>::value, "Esp32BaseConfig::getPod requires a standard-layout type");
        if (getBlob(ns, key, &out, sizeof(T))) {
            return true;
        }
        memcpy(&out, &def, sizeof(T));
        return false;
    }

    template <typename T>
    static bool setPodDeferred(const char* ns, const char* key, const T& value, uint32_t delayMs = 1000) {
        static_assert(std::is_trivially_copyable<T>::value, "Esp32BaseConfig::setPodDeferred requires a trivially copyable type");
        static_assert(std::is_standard_layout<T>::value, "Esp32BaseConfig::setPodDeferred requires a standard-layout type");
        return setBlobDeferred(ns, key, &value, sizeof(T), delayMs);
    }

    static bool flushNextDue();
    static bool flushNextForced();
    static bool flushAll();

    static uint8_t pendingCount();
    static uint8_t pendingCapacity();

    static bool clearNamespace(const char* ns);
    static bool clearLibraryNamespaces();

    static void pauseDeferredFlush();
    static void resumeDeferredFlush();
    static bool isDeferredFlushPaused();

    static void enableConfigAudit(bool enabled);
    static void enableConfigReadAudit(bool enabled);
    static bool isConfigAuditEnabled();
    static bool isConfigReadAuditEnabled();
};
