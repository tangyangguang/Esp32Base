#include "Esp32BaseConfig.h"

#if !defined(ESP32)
#error "Esp32Base supports ESP32 Arduino Core targets only."
#endif

#include "Esp32BaseLog.h"

#include <Preferences.h>
#include <string.h>

bool Esp32BaseConfig::_ready = false;
Esp32BaseConfig::PendingWrite Esp32BaseConfig::_pending[ESP32BASE_CONFIG_PENDING_MAX] = {};

bool Esp32BaseConfig::begin() {
    if (_ready) {
        return true;
    }

    _ready = true;
    ESP32BASE_LOG_I("BaseCfg", "ready=1 pending=0/%u", static_cast<unsigned>(pendingCapacity()));
    return true;
}

void Esp32BaseConfig::handle() {
    if (!_ready) {
        return;
    }

    flush();
}

bool Esp32BaseConfig::isReady() {
    return _ready;
}

bool Esp32BaseConfig::setStr(const char* ns, const char* key, const char* value) {
    if (!validateName(ns) || !validateName(key)) {
        return false;
    }

    Preferences prefs;
    if (!prefs.begin(ns, false)) {
        ESP32BASE_LOG_E("BaseCfg", "open failed ns=%s", ns);
        return false;
    }

    const char* next = value == nullptr ? "" : value;
    String current = prefs.getString(key, "");
    if (current == next) {
        prefs.end();
        return true;
    }

    size_t written = prefs.putString(key, next);
    prefs.end();
    return written == strlen(next);
}

bool Esp32BaseConfig::getStr(const char* ns, const char* key, char* out, size_t len, const char* def) {
    if (out == nullptr || len == 0U) {
        return false;
    }

    out[0] = '\0';
    if (!validateName(ns) || !validateName(key)) {
        return false;
    }

    Preferences prefs;
    if (!prefs.begin(ns, true)) {
        copyName(def == nullptr ? "" : def, out, len);
        return false;
    }

    String value = prefs.getString(key, def == nullptr ? "" : def);
    prefs.end();
    copyName(value.c_str(), out, len);
    return true;
}

bool Esp32BaseConfig::setInt(const char* ns, const char* key, int32_t value) {
    if (!validateName(ns) || !validateName(key)) {
        return false;
    }

    Preferences prefs;
    if (!prefs.begin(ns, false)) {
        ESP32BASE_LOG_E("BaseCfg", "open failed ns=%s", ns);
        return false;
    }

    if (prefs.isKey(key) && prefs.getInt(key, value) == value) {
        prefs.end();
        return true;
    }

    size_t written = prefs.putInt(key, value);
    prefs.end();
    return written == sizeof(int32_t);
}

int32_t Esp32BaseConfig::getInt(const char* ns, const char* key, int32_t def) {
    if (!validateName(ns) || !validateName(key)) {
        return def;
    }

    Preferences prefs;
    if (!prefs.begin(ns, true)) {
        return def;
    }

    int32_t value = prefs.getInt(key, def);
    prefs.end();
    return value;
}

bool Esp32BaseConfig::setBool(const char* ns, const char* key, bool value) {
    if (!validateName(ns) || !validateName(key)) {
        return false;
    }

    Preferences prefs;
    if (!prefs.begin(ns, false)) {
        ESP32BASE_LOG_E("BaseCfg", "open failed ns=%s", ns);
        return false;
    }

    if (prefs.isKey(key) && prefs.getBool(key, value) == value) {
        prefs.end();
        return true;
    }

    size_t written = prefs.putBool(key, value);
    prefs.end();
    return written == sizeof(uint8_t);
}

bool Esp32BaseConfig::getBool(const char* ns, const char* key, bool def) {
    if (!validateName(ns) || !validateName(key)) {
        return def;
    }

    Preferences prefs;
    if (!prefs.begin(ns, true)) {
        return def;
    }

    bool value = prefs.getBool(key, def);
    prefs.end();
    return value;
}

bool Esp32BaseConfig::setIntDeferred(const char* ns, const char* key, int32_t value) {
    return enqueue(PENDING_INT, ns, key, value);
}

bool Esp32BaseConfig::setBoolDeferred(const char* ns, const char* key, bool value) {
    return enqueue(PENDING_BOOL, ns, key, value ? 1 : 0);
}

bool Esp32BaseConfig::flush() {
    for (uint8_t i = 0; i < pendingCapacity(); ++i) {
        if (_pending[i].type == PENDING_EMPTY) {
            continue;
        }

        PendingWrite item = _pending[i];
        _pending[i].type = PENDING_EMPTY;
        return writePending(item);
    }

    return true;
}

uint8_t Esp32BaseConfig::pendingCount() {
    uint8_t count = 0;
    for (uint8_t i = 0; i < pendingCapacity(); ++i) {
        if (_pending[i].type != PENDING_EMPTY) {
            ++count;
        }
    }
    return count;
}

uint8_t Esp32BaseConfig::pendingCapacity() {
    return static_cast<uint8_t>(ESP32BASE_CONFIG_PENDING_MAX);
}

void Esp32BaseConfig::logConfig() {
    ESP32BASE_LOG_I("BaseCfg", "ready=%u pending=%u/%u reserved_ns=%s",
                    _ready ? 1U : 0U,
                    static_cast<unsigned>(pendingCount()),
                    static_cast<unsigned>(pendingCapacity()),
                    RESERVED_NAMESPACE);
}

bool Esp32BaseConfig::validateName(const char* name) {
    if (name == nullptr || name[0] == '\0') {
        return false;
    }

    return strlen(name) <= 15U;
}

bool Esp32BaseConfig::writePending(const PendingWrite& item) {
    switch (item.type) {
        case PENDING_INT:
            return setInt(item.ns, item.key, item.value);
        case PENDING_BOOL:
            return setBool(item.ns, item.key, item.value != 0);
        case PENDING_EMPTY:
        default:
            return true;
    }
}

bool Esp32BaseConfig::enqueue(PendingType type, const char* ns, const char* key, int32_t value) {
    if (!validateName(ns) || !validateName(key)) {
        return false;
    }

    for (uint8_t i = 0; i < pendingCapacity(); ++i) {
        if (_pending[i].type == type && strcmp(_pending[i].ns, ns) == 0 && strcmp(_pending[i].key, key) == 0) {
            _pending[i].value = value;
            return true;
        }
    }

    for (uint8_t i = 0; i < pendingCapacity(); ++i) {
        if (_pending[i].type != PENDING_EMPTY) {
            continue;
        }

        _pending[i].type = type;
        copyName(ns, _pending[i].ns, sizeof(_pending[i].ns));
        copyName(key, _pending[i].key, sizeof(_pending[i].key));
        _pending[i].value = value;
        return true;
    }

    ESP32BASE_LOG_W("BaseCfg", "pending full %u/%u",
                    static_cast<unsigned>(pendingCount()),
                    static_cast<unsigned>(pendingCapacity()));
    return false;
}

void Esp32BaseConfig::copyName(const char* in, char* out, size_t len) {
    if (out == nullptr || len == 0U) {
        return;
    }

    const char* text = in == nullptr ? "" : in;
    strncpy(out, text, len - 1U);
    out[len - 1U] = '\0';
}
