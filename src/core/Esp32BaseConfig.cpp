#include "Esp32BaseConfig.h"

#include "Esp32BaseLog.h"
#include "Esp32BaseUtil.h"

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
char g_stringScratch[CONFIG_STRING_MAX_VISIBLE_LEN + 1];
uint8_t g_blobScratch[Esp32BaseConfig::CONFIG_BLOB_MAX_LEN];

bool validName(const char* value) {
    if (!value) {
        return false;
    }
    const size_t len = strlen(value);
    return len >= 1 && len <= 15;
}

bool namespaceExists(const char* ns) {
    nvs_handle_t handle = 0;
    const esp_err_t err = nvs_open(ns, NVS_READONLY, &handle);
    if (err == ESP_OK) {
        nvs_close(handle);
        return true;
    }
    return false;
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
    if (err != ESP_OK || required == 0 || required > len) {
        nvs_close(handle);
        return false;
    }

    err = nvs_get_str(handle, key, out, &required);
    nvs_close(handle);
    if (err != ESP_OK) {
        out[0] = '\0';
        return false;
    }
    if (found) {
        *found = true;
    }
    return true;
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
    bool hadOld = false;
    const bool readOk = readStoredString(ns, key, g_stringScratch, sizeof(g_stringScratch), &hadOld);
    if (readOk && hadOld && strcmp(g_stringScratch, value) == 0) {
        if (g_auditEnabled) {
            ESP32BASE_LOG_D("config", "audit op=setStr ns=%s key=%s changed=no result=skipped", ns, key);
        }
        clearPendingKey(ns, key);
        return true;
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
    if (!readStoredString(ns, key, g_stringScratch, sizeof(g_stringScratch), &found)) {
        esp32base_internal::copySafe(out, len, def ? def : "");
        return false;
    }
    if (found) {
        esp32base_internal::copySafe(out, len, g_stringScratch);
    } else {
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
    const int slot = allocPending(ns, key);
    if (slot < 0) {
        return false;
    }
    const size_t valueLen = strlen(value);
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
        ESP32BASE_LOG_D("config", "audit op=setStrDeferred ns=%s key=%s len=%u", ns, key, static_cast<unsigned>(strlen(value)));
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
            ESP32BASE_LOG_D("config", "audit op=setBlob ns=%s key=%s len=%u changed=no result=skipped",
                            ns, key, static_cast<unsigned>(len));
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
        ESP32BASE_LOG_W("config", "audit op=setBlob ns=%s key=%s len=%u changed=yes result=failed",
                        ns, key, static_cast<unsigned>(len));
    } else if (g_auditEnabled) {
        ESP32BASE_LOG_I("config", "audit op=setBlob ns=%s key=%s len=%u changed=yes result=success",
                        ns, key, static_cast<unsigned>(len));
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
        ESP32BASE_LOG_D("config", "audit op=getBlob ns=%s key=%s found=%s len=%u",
                        ns, key, found ? "yes" : "no", static_cast<unsigned>(actualLen));
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
            ESP32BASE_LOG_D("config", "audit op=setBlobDeferred ns=%s key=%s len=%u changed=no result=skipped_pending",
                            ns, key, static_cast<unsigned>(len));
        }
        return true;
    }

    bool hadOld = false;
    size_t oldLen = 0;
    const bool readOk = readStoredBlob(ns, key, g_blobScratch, sizeof(g_blobScratch), &oldLen, &hadOld);
    if (readOk && hadOld && oldLen == len && memcmp(g_blobScratch, data, len) == 0) {
        if (g_auditEnabled) {
            ESP32BASE_LOG_D("config", "audit op=setBlobDeferred ns=%s key=%s len=%u changed=no result=skipped",
                            ns, key, static_cast<unsigned>(len));
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
        ESP32BASE_LOG_D("config", "audit op=setBlobDeferred ns=%s key=%s len=%u",
                        ns, key, static_cast<unsigned>(len));
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
    if (!namespaceExists(ns)) {
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
    if (!namespaceExists("eb_sys")) {
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
    ok = clearWebAuthConfig() && ok;
    ok = clearSystemConfig() && ok;
    ok = clearLogConfig() && ok;
    ok = clearUiConfig() && ok;
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
