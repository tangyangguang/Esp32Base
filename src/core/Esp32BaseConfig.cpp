#include "Esp32BaseConfig.h"

#include "../Esp32BaseProfile.h"
#include "Esp32BaseLog.h"
#include "Esp32BaseUtil.h"
#include "internal/Esp32BaseConfigInternal.h"

#include <Preferences.h>
#include <nvs.h>
#include <stdlib.h>
#include <string.h>

namespace {
constexpr size_t CONFIG_STRING_MAX_VISIBLE_LEN = 3999;

enum PendingType : uint8_t {
    PENDING_EMPTY = 0,
    PENDING_INT,
    PENDING_BOOL,
    PENDING_STR,
    PENDING_BLOB
};

struct PendingItem {
    PendingType type;
    char ns[16];
    char key[16];
    char* strValue;
    uint8_t* blobValue;
    size_t blobLen;
    int32_t intValue;
    bool boolValue;
    uint32_t dueMs;
};

bool g_ready = false;
bool g_paused = false;
bool g_auditEnabled = false;
bool g_readAuditEnabled = false;
PendingItem g_pending[ESP32BASE_CONFIG_PENDING_MAX];
uint8_t g_blobScratch[Esp32BaseConfig::CONFIG_BLOB_MAX_LEN];

bool validName(const char* value) {
    if (!value) {
        return false;
    }
    const size_t len = strlen(value);
    return len >= 1 && len <= 15;
}

enum class NamespaceLookupResult : uint8_t {
    Found,
    NotFound,
    Error
};

NamespaceLookupResult lookupNamespace(const char* ns) {
    if (!validName(ns)) {
        return NamespaceLookupResult::Error;
    }
    nvs_handle_t handle = 0;
    const esp_err_t err = nvs_open(ns, NVS_READONLY, &handle);
    if (err == ESP_OK) {
        nvs_close(handle);
        return NamespaceLookupResult::Found;
    }
    return err == ESP_ERR_NVS_NOT_FOUND
               ? NamespaceLookupResult::NotFound
               : NamespaceLookupResult::Error;
}

bool namespaceExists(const char* ns) {
    return lookupNamespace(ns) == NamespaceLookupResult::Found;
}

bool readStoredString(const char* ns, const char* key, char* out, size_t len, bool* found) {
    if (found) {
        *found = false;
    }
    if (!out || len == 0) {
        return false;
    }
    out[0] = '\0';

    nvs_handle_t handle = 0;
    const esp_err_t openErr = nvs_open(ns, NVS_READONLY, &handle);
    if (openErr == ESP_ERR_NVS_NOT_FOUND) {
        return true;
    }
    if (openErr != ESP_OK) {
        return false;
    }

    size_t required = 0;
    esp_err_t err = nvs_get_str(handle, key, nullptr, &required);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(handle);
        return true;
    }
    if (err != ESP_OK || required == 0 || required > CONFIG_STRING_MAX_VISIBLE_LEN + 1U) {
        nvs_close(handle);
        return false;
    }

    char* readBuffer = out;
    if (required > len) {
        readBuffer = static_cast<char*>(malloc(required));
        if (!readBuffer) {
            nvs_close(handle);
            return false;
        }
    }
    size_t readLength = required;
    err = nvs_get_str(handle, key, readBuffer, &readLength);
    nvs_close(handle);
    if (err != ESP_OK) {
        if (readBuffer != out) {
            free(readBuffer);
        }
        out[0] = '\0';
        return false;
    }
    if (readBuffer != out) {
        esp32base_internal::copySafe(out, len, readBuffer);
        free(readBuffer);
    }
    if (found) {
        *found = true;
    }
    return true;
}

enum class StoredStringCompareResult : uint8_t {
    NotFound,
    Equal,
    Different,
    Error
};

StoredStringCompareResult compareStoredString(const char* ns, const char* key,
                                              const char* value) {
    if (!value) {
        return StoredStringCompareResult::Error;
    }

    nvs_handle_t handle = 0;
    const esp_err_t openErr = nvs_open(ns, NVS_READONLY, &handle);
    if (openErr == ESP_ERR_NVS_NOT_FOUND) {
        return StoredStringCompareResult::NotFound;
    }
    if (openErr != ESP_OK) {
        return StoredStringCompareResult::Error;
    }

    size_t required = 0;
    esp_err_t err = nvs_get_str(handle, key, nullptr, &required);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(handle);
        return StoredStringCompareResult::NotFound;
    }
    if (err == ESP_ERR_NVS_TYPE_MISMATCH) {
        nvs_close(handle);
        return StoredStringCompareResult::Different;
    }
    if (err != ESP_OK || required == 0 || required > CONFIG_STRING_MAX_VISIBLE_LEN + 1U) {
        nvs_close(handle);
        return StoredStringCompareResult::Error;
    }
    if (required != strlen(value) + 1U) {
        nvs_close(handle);
        return StoredStringCompareResult::Different;
    }

    char* readBuffer = required <= sizeof(g_blobScratch)
        ? reinterpret_cast<char*>(g_blobScratch)
        : static_cast<char*>(malloc(required));
    if (!readBuffer) {
        nvs_close(handle);
        return StoredStringCompareResult::Error;
    }
    size_t readLength = required;
    err = nvs_get_str(handle, key, readBuffer, &readLength);
    nvs_close(handle);
    const StoredStringCompareResult result =
        err == ESP_OK
            ? (strcmp(readBuffer, value) == 0
                   ? StoredStringCompareResult::Equal
                   : StoredStringCompareResult::Different)
            : StoredStringCompareResult::Error;
    if (readBuffer != reinterpret_cast<char*>(g_blobScratch)) {
        free(readBuffer);
    }
    return result;
}

bool readStoredBlob(const char* ns, const char* key, void* out, size_t len, size_t* actualLen, bool* found) {
    if (actualLen) {
        *actualLen = 0;
    }
    if (found) {
        *found = false;
    }
    if (!out || len == 0 || len > Esp32BaseConfig::CONFIG_BLOB_MAX_LEN) {
        return false;
    }

    nvs_handle_t handle = 0;
    const esp_err_t openErr = nvs_open(ns, NVS_READONLY, &handle);
    if (openErr == ESP_ERR_NVS_NOT_FOUND) {
        return true;
    }
    if (openErr != ESP_OK) {
        return false;
    }

    size_t required = 0;
    esp_err_t err = nvs_get_blob(handle, key, nullptr, &required);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(handle);
        return true;
    }
    if (err != ESP_OK || required == 0 || required > len || required > Esp32BaseConfig::CONFIG_BLOB_MAX_LEN) {
        nvs_close(handle);
        return false;
    }

    err = nvs_get_blob(handle, key, out, &required);
    nvs_close(handle);
    if (err != ESP_OK) {
        return false;
    }
    if (actualLen) {
        *actualLen = required;
    }
    if (found) {
        *found = true;
    }
    return true;
}

bool readStoredInt(const char* ns, const char* key, int32_t* out, bool* found) {
    if (found) {
        *found = false;
    }
    if (!out) {
        return false;
    }
    nvs_handle_t handle = 0;
    const esp_err_t openErr = nvs_open(ns, NVS_READONLY, &handle);
    if (openErr == ESP_ERR_NVS_NOT_FOUND) {
        return true;
    }
    if (openErr != ESP_OK) {
        return false;
    }

    int32_t value = 0;
    const esp_err_t err = nvs_get_i32(handle, key, &value);
    nvs_close(handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return true;
    }
    if (err != ESP_OK) {
        return false;
    }
    *out = value;
    if (found) {
        *found = true;
    }
    return true;
}

bool readStoredBool(const char* ns, const char* key, bool* out, bool* found) {
    if (found) {
        *found = false;
    }
    if (!out) {
        return false;
    }
    nvs_handle_t handle = 0;
    const esp_err_t openErr = nvs_open(ns, NVS_READONLY, &handle);
    if (openErr == ESP_ERR_NVS_NOT_FOUND) {
        return true;
    }
    if (openErr != ESP_OK) {
        return false;
    }

    uint8_t value = 0;
    const esp_err_t err = nvs_get_u8(handle, key, &value);
    nvs_close(handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return true;
    }
    if (err != ESP_OK) {
        return false;
    }
    *out = value != 0;
    if (found) {
        *found = true;
    }
    return true;
}

int findPending(const char* ns, const char* key) {
    for (uint8_t i = 0; i < ESP32BASE_CONFIG_PENDING_MAX; ++i) {
        if (g_pending[i].type != PENDING_EMPTY && strcmp(g_pending[i].ns, ns) == 0 && strcmp(g_pending[i].key, key) == 0) {
            return i;
        }
    }
    return -1;
}

int allocPending(const char* ns, const char* key) {
    const int existing = findPending(ns, key);
    if (existing >= 0) {
        return existing;
    }
    for (uint8_t i = 0; i < ESP32BASE_CONFIG_PENDING_MAX; ++i) {
        if (g_pending[i].type == PENDING_EMPTY) {
            esp32base_internal::copySafe(g_pending[i].ns, sizeof(g_pending[i].ns), ns);
            esp32base_internal::copySafe(g_pending[i].key, sizeof(g_pending[i].key), key);
            return i;
        }
    }
    return -1;
}

void clearPendingString(PendingItem& item) {
    if (item.type == PENDING_STR && item.strValue) {
        free(item.strValue);
        item.strValue = nullptr;
    }
}

void clearPendingBlob(PendingItem& item) {
    if (item.type == PENDING_BLOB && item.blobValue) {
        free(item.blobValue);
        item.blobValue = nullptr;
    }
    item.blobLen = 0;
}

void clearPendingItem(PendingItem& item) {
    clearPendingString(item);
    clearPendingBlob(item);
    item.type = PENDING_EMPTY;
    item.ns[0] = '\0';
    item.key[0] = '\0';
}

void clearPendingKey(const char* ns, const char* key) {
    const int pending = findPending(ns, key);
    if (pending >= 0) {
        clearPendingItem(g_pending[pending]);
    }
}

void clearPendingNamespace(const char* ns) {
    for (uint8_t i = 0; i < ESP32BASE_CONFIG_PENDING_MAX; ++i) {
        if (g_pending[i].type != PENDING_EMPTY && strcmp(g_pending[i].ns, ns) == 0) {
            clearPendingItem(g_pending[i]);
        }
    }
}

bool writePending(PendingItem& item) {
    Preferences prefs;
    if (!prefs.begin(item.ns, false)) {
        return false;
    }

    bool ok = false;
    switch (item.type) {
        case PENDING_INT:
            ok = prefs.putInt(item.key, item.intValue) > 0;
            break;
        case PENDING_BOOL:
            ok = prefs.putBool(item.key, item.boolValue) > 0;
            break;
        case PENDING_STR:
            {
                const char* value = item.strValue ? item.strValue : "";
                const size_t written = prefs.putString(item.key, value);
                ok = value[0] == '\0' ? prefs.isKey(item.key) : written > 0;
            }
            break;
        case PENDING_BLOB:
            ok = item.blobValue && item.blobLen > 0 && prefs.putBytes(item.key, item.blobValue, item.blobLen) == item.blobLen;
            break;
        default:
            ok = true;
            break;
    }
    prefs.end();
    if (g_auditEnabled) {
        if (ok) {
            ESP32BASE_LOG_I("config", "audit op=flush ns=%s key=%s type=%u result=success",
                            item.ns, item.key, static_cast<unsigned>(item.type));
        } else {
            ESP32BASE_LOG_W("config", "audit op=flush ns=%s key=%s type=%u result=failed",
                            item.ns, item.key, static_cast<unsigned>(item.type));
        }
    }
    return ok;
}

bool flushIndex(uint8_t index) {
    if (index >= ESP32BASE_CONFIG_PENDING_MAX || g_pending[index].type == PENDING_EMPTY) {
        return false;
    }
    if (!writePending(g_pending[index])) {
        ESP32BASE_LOG_E("config", "deferred flush failed: %s.%s", g_pending[index].ns, g_pending[index].key);
        return false;
    }
    clearPendingItem(g_pending[index]);
    return true;
}
}

esp32base_internal::ConfigUInt32ReadResult esp32base_internal::readConfigUInt32(
    const char* ns,
    const char* key,
    uint32_t& value) {
    if (!validName(ns) || !validName(key)) {
        return ConfigUInt32ReadResult::Error;
    }
    nvs_handle_t handle = 0;
    const esp_err_t openErr = nvs_open(ns, NVS_READONLY, &handle);
    if (openErr == ESP_ERR_NVS_NOT_FOUND) {
        value = 0;
        return ConfigUInt32ReadResult::NotFound;
    }
    if (openErr != ESP_OK) {
        return ConfigUInt32ReadResult::Error;
    }
    const esp_err_t readErr = nvs_get_u32(handle, key, &value);
    nvs_close(handle);
    if (readErr == ESP_ERR_NVS_NOT_FOUND) {
        value = 0;
        return ConfigUInt32ReadResult::NotFound;
    }
    return readErr == ESP_OK ? ConfigUInt32ReadResult::Found
                             : ConfigUInt32ReadResult::Error;
}

bool esp32base_internal::writeConfigUInt32(const char* ns, const char* key, uint32_t value) {
    if (!validName(ns) || !validName(key)) {
        return false;
    }
    uint32_t storedValue = 0;
    const ConfigUInt32ReadResult readResult = readConfigUInt32(ns, key, storedValue);
    if (readResult == ConfigUInt32ReadResult::Found && storedValue == value) {
        return true;
    }
    Preferences prefs;
    if (!prefs.begin(ns, false)) {
        return false;
    }
    const bool ok = prefs.putUInt(key, value) > 0;
    prefs.end();
    return ok;
}

esp32base_internal::ConfigKeyRemoveResult esp32base_internal::removeConfigKey(const char* ns, const char* key) {
    if (!validName(ns) || !validName(key)) {
        return ConfigKeyRemoveResult::Error;
    }
    const bool hadPending = findPending(ns, key) >= 0;
    const NamespaceLookupResult lookup = lookupNamespace(ns);
    if (lookup == NamespaceLookupResult::Error) {
        return ConfigKeyRemoveResult::Error;
    }
    if (lookup == NamespaceLookupResult::NotFound) {
        clearPendingKey(ns, key);
        return hadPending ? ConfigKeyRemoveResult::Removed : ConfigKeyRemoveResult::NotFound;
    }
    Preferences prefs;
    if (!prefs.begin(ns, false)) {
        return ConfigKeyRemoveResult::Error;
    }
    const bool existed = prefs.isKey(key);
    const bool ok = !existed || prefs.remove(key);
    prefs.end();
    if (!ok) {
        return ConfigKeyRemoveResult::Error;
    }
    clearPendingKey(ns, key);
    return existed || hadPending ? ConfigKeyRemoveResult::Removed : ConfigKeyRemoveResult::NotFound;
}

bool Esp32BaseConfig::begin() {
    g_ready = true;
    return true;
}

void Esp32BaseConfig::handle() {
    flushNextDue();
}

bool Esp32BaseConfig::isReady() {
    return g_ready;
}

bool Esp32BaseConfig::setStr(const char* ns, const char* key, const char* value) {
    if (!validName(ns) || !validName(key) || !value || strlen(value) > CONFIG_STRING_MAX_VISIBLE_LEN) {
        return false;
    }
    const StoredStringCompareResult compareResult =
        compareStoredString(ns, key, value);
    if (compareResult == StoredStringCompareResult::Equal) {
        if (g_auditEnabled) {
            ESP32BASE_LOG_D("config", "audit op=setStr ns=%s key=%s changed=no result=skipped", ns, key);
        }
        clearPendingKey(ns, key);
        return true;
    }
    if (compareResult == StoredStringCompareResult::Error) {
        ESP32BASE_LOG_W("config", "audit op=setStr ns=%s key=%s result=compare_failed", ns, key);
        return false;
    }

    Preferences prefs;
    if (!prefs.begin(ns, false)) {
        return false;
    }
    const size_t written = prefs.putString(key, value);
    const bool ok = value[0] == '\0' ? prefs.isKey(key) : written > 0;
    prefs.end();
    if (!ok) {
        ESP32BASE_LOG_W("config", "audit op=setStr ns=%s key=%s changed=yes result=failed", ns, key);
    } else if (g_auditEnabled) {
        ESP32BASE_LOG_I("config", "audit op=setStr ns=%s key=%s changed=yes result=%s", ns, key, ok ? "success" : "failed");
    }
    if (ok) {
        clearPendingKey(ns, key);
    }
    return ok;
}

bool Esp32BaseConfig::getStr(const char* ns, const char* key, char* out, size_t len, const char* def) {
    if (!out || len == 0 || !validName(ns) || !validName(key)) {
        return false;
    }

    const int pending = findPending(ns, key);
    if (pending >= 0 && g_pending[pending].type == PENDING_STR) {
        esp32base_internal::copySafe(out, len, g_pending[pending].strValue ? g_pending[pending].strValue : "");
        return true;
    }

    bool found = false;
    if (!readStoredString(ns, key, out, len, &found)) {
        esp32base_internal::copySafe(out, len, def ? def : "");
        return false;
    }
    if (!found) {
        esp32base_internal::copySafe(out, len, def ? def : "");
    }
    if (g_readAuditEnabled) {
        ESP32BASE_LOG_D("config", "audit op=getStr ns=%s key=%s found=%s value=%s", ns, key, found ? "yes" : "no", out);
    }
    return found;
}

bool Esp32BaseConfig::setInt(const char* ns, const char* key, int32_t value) {
    if (!validName(ns) || !validName(key)) {
        return false;
    }
    Preferences prefs;
    if (!prefs.begin(ns, false)) {
        return false;
    }
    const bool hadOld = prefs.isKey(key);
    if (hadOld && prefs.getInt(key, value) == value) {
        prefs.end();
        if (g_auditEnabled) {
            ESP32BASE_LOG_D("config", "audit op=setInt ns=%s key=%s changed=no result=skipped", ns, key);
        }
        clearPendingKey(ns, key);
        return true;
    }
    const bool ok = prefs.putInt(key, value) > 0;
    prefs.end();
    if (!ok) {
        ESP32BASE_LOG_W("config", "audit op=setInt ns=%s key=%s changed=yes value=%ld result=failed",
                        ns, key, static_cast<long>(value));
    } else if (g_auditEnabled) {
        ESP32BASE_LOG_I("config", "audit op=setInt ns=%s key=%s changed=yes value=%ld result=%s",
                        ns, key, static_cast<long>(value), ok ? "success" : "failed");
    }
    if (ok) {
        clearPendingKey(ns, key);
    }
    return ok;
}

int32_t Esp32BaseConfig::getInt(const char* ns, const char* key, int32_t def) {
    if (!validName(ns) || !validName(key)) {
        return def;
    }
    const int pending = findPending(ns, key);
    if (pending >= 0 && g_pending[pending].type == PENDING_INT) {
        return g_pending[pending].intValue;
    }
    if (!namespaceExists(ns)) {
        return def;
    }
    Preferences prefs;
    if (!prefs.begin(ns, true)) {
        return def;
    }
    const bool found = prefs.isKey(key);
    const int32_t value = found ? prefs.getInt(key, def) : def;
    prefs.end();
    if (g_readAuditEnabled) {
        ESP32BASE_LOG_D("config", "audit op=getInt ns=%s key=%s found=%s value=%ld",
                        ns, key, found ? "yes" : "no", static_cast<long>(value));
    }
    return value;
}

bool Esp32BaseConfig::setIntDeferred(const char* ns, const char* key, int32_t value, uint32_t delayMs) {
    if (!validName(ns) || !validName(key)) {
        return false;
    }
    const int existing = findPending(ns, key);
    if (existing >= 0 && g_pending[existing].type == PENDING_INT && g_pending[existing].intValue == value) {
        if (g_auditEnabled) {
            ESP32BASE_LOG_D("config", "audit op=setIntDeferred ns=%s key=%s value=%ld changed=no result=skipped_pending",
                            ns, key, static_cast<long>(value));
        }
        return true;
    }

    bool hadOld = false;
    int32_t oldValue = 0;
    const bool readOk = readStoredInt(ns, key, &oldValue, &hadOld);
    if (readOk && hadOld && oldValue == value) {
        if (g_auditEnabled) {
            ESP32BASE_LOG_D("config", "audit op=setIntDeferred ns=%s key=%s value=%ld changed=no result=skipped",
                            ns, key, static_cast<long>(value));
        }
        clearPendingKey(ns, key);
        return true;
    }

    const int slot = allocPending(ns, key);
    if (slot < 0) {
        return false;
    }
    clearPendingString(g_pending[slot]);
    clearPendingBlob(g_pending[slot]);
    g_pending[slot].type = PENDING_INT;
    g_pending[slot].intValue = value;
    g_pending[slot].dueMs = millis() + delayMs;
    if (g_auditEnabled) {
        ESP32BASE_LOG_D("config", "audit op=setIntDeferred ns=%s key=%s value=%ld", ns, key, static_cast<long>(value));
    }
    return true;
}

bool Esp32BaseConfig::setBool(const char* ns, const char* key, bool value) {
    if (!validName(ns) || !validName(key)) {
        return false;
    }
    Preferences prefs;
    if (!prefs.begin(ns, false)) {
        return false;
    }
    const bool hadOld = prefs.isKey(key);
    if (hadOld && prefs.getBool(key, value) == value) {
        prefs.end();
        if (g_auditEnabled) {
            ESP32BASE_LOG_D("config", "audit op=setBool ns=%s key=%s changed=no result=skipped", ns, key);
        }
        clearPendingKey(ns, key);
        return true;
    }
    const bool ok = prefs.putBool(key, value) > 0;
    prefs.end();
    if (!ok) {
        ESP32BASE_LOG_W("config", "audit op=setBool ns=%s key=%s changed=yes value=%u result=failed",
                        ns, key, value ? 1U : 0U);
    } else if (g_auditEnabled) {
        ESP32BASE_LOG_I("config", "audit op=setBool ns=%s key=%s changed=yes value=%u result=%s",
                        ns, key, value ? 1U : 0U, ok ? "success" : "failed");
    }
    if (ok) {
        clearPendingKey(ns, key);
    }
    return ok;
}

bool Esp32BaseConfig::getBool(const char* ns, const char* key, bool def) {
    if (!validName(ns) || !validName(key)) {
        return def;
    }
    const int pending = findPending(ns, key);
    if (pending >= 0 && g_pending[pending].type == PENDING_BOOL) {
        return g_pending[pending].boolValue;
    }
    if (!namespaceExists(ns)) {
        return def;
    }
    Preferences prefs;
    if (!prefs.begin(ns, true)) {
        return def;
    }
    const bool found = prefs.isKey(key);
    const bool value = found ? prefs.getBool(key, def) : def;
    prefs.end();
    if (g_readAuditEnabled) {
        ESP32BASE_LOG_D("config", "audit op=getBool ns=%s key=%s found=%s value=%u",
                        ns, key, found ? "yes" : "no", value ? 1U : 0U);
    }
    return value;
}

bool Esp32BaseConfig::setBoolDeferred(const char* ns, const char* key, bool value, uint32_t delayMs) {
    if (!validName(ns) || !validName(key)) {
        return false;
    }
    const int existing = findPending(ns, key);
    if (existing >= 0 && g_pending[existing].type == PENDING_BOOL && g_pending[existing].boolValue == value) {
        if (g_auditEnabled) {
            ESP32BASE_LOG_D("config", "audit op=setBoolDeferred ns=%s key=%s value=%u changed=no result=skipped_pending",
                            ns, key, value ? 1U : 0U);
        }
        return true;
    }

    bool hadOld = false;
    bool oldValue = false;
    const bool readOk = readStoredBool(ns, key, &oldValue, &hadOld);
    if (readOk && hadOld && oldValue == value) {
        if (g_auditEnabled) {
            ESP32BASE_LOG_D("config", "audit op=setBoolDeferred ns=%s key=%s value=%u changed=no result=skipped",
                            ns, key, value ? 1U : 0U);
        }
        clearPendingKey(ns, key);
        return true;
    }

    const int slot = allocPending(ns, key);
    if (slot < 0) {
        return false;
    }
    clearPendingString(g_pending[slot]);
    clearPendingBlob(g_pending[slot]);
    g_pending[slot].type = PENDING_BOOL;
    g_pending[slot].boolValue = value;
    g_pending[slot].dueMs = millis() + delayMs;
    if (g_auditEnabled) {
        ESP32BASE_LOG_D("config", "audit op=setBoolDeferred ns=%s key=%s value=%u", ns, key, value ? 1U : 0U);
    }
    return true;
}

bool Esp32BaseConfig::setStrDeferred(const char* ns, const char* key, const char* value, uint32_t delayMs) {
    if (!validName(ns) || !validName(key) || !value || strlen(value) > 3999) {
        return false;
    }
    const size_t valueLen = strlen(value);
    const int existing = findPending(ns, key);
    if (existing >= 0 && g_pending[existing].type == PENDING_STR &&
        strcmp(g_pending[existing].strValue ? g_pending[existing].strValue : "", value) == 0) {
        if (g_auditEnabled) {
            char lenBuf[24];
            Esp32BaseLog::formatBytes(valueLen, lenBuf, sizeof(lenBuf));
            ESP32BASE_LOG_D("config", "audit op=setStrDeferred ns=%s key=%s len=%s changed=no result=skipped_pending",
                            ns, key, lenBuf);
        }
        return true;
    }

    const StoredStringCompareResult compareResult =
        compareStoredString(ns, key, value);
    if (compareResult == StoredStringCompareResult::Equal) {
        if (g_auditEnabled) {
            char lenBuf[24];
            Esp32BaseLog::formatBytes(valueLen, lenBuf, sizeof(lenBuf));
            ESP32BASE_LOG_D("config", "audit op=setStrDeferred ns=%s key=%s len=%s changed=no result=skipped",
                            ns, key, lenBuf);
        }
        clearPendingKey(ns, key);
        return true;
    }
    if (compareResult == StoredStringCompareResult::Error) {
        ESP32BASE_LOG_W("config",
                        "audit op=setStrDeferred ns=%s key=%s result=compare_failed",
                        ns, key);
        return false;
    }

    const int slot = allocPending(ns, key);
    if (slot < 0) {
        return false;
    }
    char* copy = static_cast<char*>(malloc(valueLen + 1));
    if (!copy) {
        return false;
    }
    memcpy(copy, value, valueLen + 1);
    clearPendingString(g_pending[slot]);
    clearPendingBlob(g_pending[slot]);
    g_pending[slot].type = PENDING_STR;
    g_pending[slot].strValue = copy;
    g_pending[slot].dueMs = millis() + delayMs;
    if (g_auditEnabled) {
        char lenBuf[24];
        Esp32BaseLog::formatBytes(valueLen, lenBuf, sizeof(lenBuf));
        ESP32BASE_LOG_D("config", "audit op=setStrDeferred ns=%s key=%s len=%s", ns, key, lenBuf);
    }
    return true;
}

bool Esp32BaseConfig::setBlob(const char* ns, const char* key, const void* data, size_t len) {
    if (!validName(ns) || !validName(key) || !data || len == 0 || len > CONFIG_BLOB_MAX_LEN) {
        return false;
    }
    bool hadOld = false;
    size_t oldLen = 0;
    const bool readOk = readStoredBlob(ns, key, g_blobScratch, sizeof(g_blobScratch), &oldLen, &hadOld);
    if (readOk && hadOld && oldLen == len && memcmp(g_blobScratch, data, len) == 0) {
        if (g_auditEnabled) {
            char lenBuf[24];
            Esp32BaseLog::formatBytes(len, lenBuf, sizeof(lenBuf));
            ESP32BASE_LOG_D("config", "audit op=setBlob ns=%s key=%s len=%s changed=no result=skipped",
                            ns, key, lenBuf);
        }
        clearPendingKey(ns, key);
        return true;
    }

    Preferences prefs;
    if (!prefs.begin(ns, false)) {
        return false;
    }
    const bool ok = prefs.putBytes(key, data, len) == len;
    prefs.end();
    if (!ok) {
        char lenBuf[24];
        Esp32BaseLog::formatBytes(len, lenBuf, sizeof(lenBuf));
        ESP32BASE_LOG_W("config", "audit op=setBlob ns=%s key=%s len=%s changed=yes result=failed",
                        ns, key, lenBuf);
    } else if (g_auditEnabled) {
        char lenBuf[24];
        Esp32BaseLog::formatBytes(len, lenBuf, sizeof(lenBuf));
        ESP32BASE_LOG_I("config", "audit op=setBlob ns=%s key=%s len=%s changed=yes result=success",
                        ns, key, lenBuf);
    }
    if (ok) {
        clearPendingKey(ns, key);
    }
    return ok;
}

bool Esp32BaseConfig::getBlob(const char* ns, const char* key, void* out, size_t len) {
    if (!validName(ns) || !validName(key) || !out || len == 0 || len > CONFIG_BLOB_MAX_LEN) {
        return false;
    }

    const int pending = findPending(ns, key);
    if (pending >= 0 && g_pending[pending].type == PENDING_BLOB) {
        if (g_pending[pending].blobLen != len || !g_pending[pending].blobValue) {
            return false;
        }
        memcpy(out, g_pending[pending].blobValue, len);
        return true;
    }

    bool found = false;
    size_t actualLen = 0;
    const bool ok = readStoredBlob(ns, key, out, len, &actualLen, &found);
    if (g_readAuditEnabled) {
        char lenBuf[24];
        Esp32BaseLog::formatBytes(actualLen, lenBuf, sizeof(lenBuf));
        ESP32BASE_LOG_D("config", "audit op=getBlob ns=%s key=%s found=%s len=%s",
                        ns, key, found ? "yes" : "no", lenBuf);
    }
    return ok && found && actualLen == len;
}

bool Esp32BaseConfig::setBlobDeferred(const char* ns, const char* key, const void* data, size_t len, uint32_t delayMs) {
    if (!validName(ns) || !validName(key) || !data || len == 0 || len > CONFIG_BLOB_MAX_LEN) {
        return false;
    }
    const int existing = findPending(ns, key);
    if (existing >= 0 && g_pending[existing].type == PENDING_BLOB && g_pending[existing].blobLen == len &&
        g_pending[existing].blobValue && memcmp(g_pending[existing].blobValue, data, len) == 0) {
        if (g_auditEnabled) {
            char lenBuf[24];
            Esp32BaseLog::formatBytes(len, lenBuf, sizeof(lenBuf));
            ESP32BASE_LOG_D("config", "audit op=setBlobDeferred ns=%s key=%s len=%s changed=no result=skipped_pending",
                            ns, key, lenBuf);
        }
        return true;
    }

    bool hadOld = false;
    size_t oldLen = 0;
    const bool readOk = readStoredBlob(ns, key, g_blobScratch, sizeof(g_blobScratch), &oldLen, &hadOld);
    if (readOk && hadOld && oldLen == len && memcmp(g_blobScratch, data, len) == 0) {
        if (g_auditEnabled) {
            char lenBuf[24];
            Esp32BaseLog::formatBytes(len, lenBuf, sizeof(lenBuf));
            ESP32BASE_LOG_D("config", "audit op=setBlobDeferred ns=%s key=%s len=%s changed=no result=skipped",
                            ns, key, lenBuf);
        }
        clearPendingKey(ns, key);
        return true;
    }

    const int slot = allocPending(ns, key);
    if (slot < 0) {
        return false;
    }
    uint8_t* copy = static_cast<uint8_t*>(malloc(len));
    if (!copy) {
        return false;
    }
    memcpy(copy, data, len);
    clearPendingString(g_pending[slot]);
    clearPendingBlob(g_pending[slot]);
    g_pending[slot].type = PENDING_BLOB;
    g_pending[slot].blobValue = copy;
    g_pending[slot].blobLen = len;
    g_pending[slot].dueMs = millis() + delayMs;
    if (g_auditEnabled) {
        char lenBuf[24];
        Esp32BaseLog::formatBytes(len, lenBuf, sizeof(lenBuf));
        ESP32BASE_LOG_D("config", "audit op=setBlobDeferred ns=%s key=%s len=%s",
                        ns, key, lenBuf);
    }
    return true;
}

bool Esp32BaseConfig::flushNextDue() {
    if (g_paused) {
        return false;
    }
    const uint32_t now = millis();
    for (uint8_t i = 0; i < ESP32BASE_CONFIG_PENDING_MAX; ++i) {
        if (g_pending[i].type != PENDING_EMPTY && static_cast<int32_t>(now - g_pending[i].dueMs) >= 0) {
            return flushIndex(i);
        }
    }
    return false;
}

bool Esp32BaseConfig::flushNextForced() {
    for (uint8_t i = 0; i < ESP32BASE_CONFIG_PENDING_MAX; ++i) {
        if (g_pending[i].type != PENDING_EMPTY) {
            return flushIndex(i);
        }
    }
    return false;
}

bool Esp32BaseConfig::flushAll() {
    bool ok = true;
    for (uint8_t i = 0; i < ESP32BASE_CONFIG_PENDING_MAX; ++i) {
        if (g_pending[i].type != PENDING_EMPTY && !flushIndex(i)) {
            ok = false;
        }
    }
    return ok;
}

uint8_t Esp32BaseConfig::pendingCount() {
    uint8_t count = 0;
    for (uint8_t i = 0; i < ESP32BASE_CONFIG_PENDING_MAX; ++i) {
        if (g_pending[i].type != PENDING_EMPTY) {
            ++count;
        }
    }
    return count;
}

uint8_t Esp32BaseConfig::pendingCapacity() {
    return ESP32BASE_CONFIG_PENDING_MAX;
}

bool Esp32BaseConfig::clearNamespace(const char* ns) {
    if (!validName(ns)) {
        return false;
    }
    const NamespaceLookupResult lookup = lookupNamespace(ns);
    if (lookup == NamespaceLookupResult::Error) {
        return false;
    }
    if (lookup == NamespaceLookupResult::NotFound) {
        clearPendingNamespace(ns);
        return true;
    }
    Preferences prefs;
    if (!prefs.begin(ns, false)) {
        return false;
    }
    const bool ok = prefs.clear();
    prefs.end();
    if (ok) {
        clearPendingNamespace(ns);
    }
    return ok;
}

bool Esp32BaseConfig::clearWifiConfig() {
    return clearNamespace("eb_wifi");
}

bool Esp32BaseConfig::clearWebAuthConfig() {
    return clearNamespace("eb_web");
}

bool Esp32BaseConfig::clearSystemConfig() {
    clearPendingKey("eb_sys", "hostname");
    const NamespaceLookupResult lookup = lookupNamespace("eb_sys");
    if (lookup == NamespaceLookupResult::Error) {
        return false;
    }
    if (lookup == NamespaceLookupResult::NotFound) {
        return true;
    }
    Preferences prefs;
    if (!prefs.begin("eb_sys", false)) {
        return false;
    }
    const bool ok = !prefs.isKey("hostname") || prefs.remove("hostname");
    prefs.end();
    return ok;
}

bool Esp32BaseConfig::clearLogConfig() {
    return clearNamespace("eb_log");
}

bool Esp32BaseConfig::clearUiConfig() {
    return clearNamespace("eb_ui");
}

bool Esp32BaseConfig::factoryReset() {
    bool ok = true;
    ok = clearWifiConfig() && ok;
    ok = clearNamespace("eb_wifi_rcv") && ok;
    ok = clearWebAuthConfig() && ok;
    ok = clearSystemConfig() && ok;
    ok = clearLogConfig() && ok;
    ok = clearUiConfig() && ok;
#if ESP32BASE_ENABLE_APP_EVENT_CONDITIONS
    ok = clearNamespace("eb_app_events") && ok;
#endif
    return ok;
}

bool Esp32BaseConfig::clearLibraryNamespaces() {
    return factoryReset();
}

void Esp32BaseConfig::pauseDeferredFlush() {
    g_paused = true;
}

void Esp32BaseConfig::resumeDeferredFlush() {
    g_paused = false;
}

bool Esp32BaseConfig::isDeferredFlushPaused() {
    return g_paused;
}

void Esp32BaseConfig::enableConfigAudit(bool enabled) {
    g_auditEnabled = enabled;
}

void Esp32BaseConfig::enableConfigReadAudit(bool enabled) {
    g_readAuditEnabled = enabled;
}

bool Esp32BaseConfig::isConfigAuditEnabled() {
    return g_auditEnabled;
}

bool Esp32BaseConfig::isConfigReadAuditEnabled() {
    return g_readAuditEnabled;
}
