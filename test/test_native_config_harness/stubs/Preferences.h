#pragma once

#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace native_nvs {

enum class ValueType {
    Int,
    UInt,
    Bool,
    String,
    Blob,
};

struct Value {
    ValueType type;
    int32_t intValue = 0;
    uint32_t uintValue = 0;
    bool boolValue = false;
    std::string stringValue;
    std::vector<uint8_t> blobValue;
};

inline std::map<std::string, std::map<std::string, Value>>& store() {
    static std::map<std::string, std::map<std::string, Value>> data;
    return data;
}

inline unsigned& writeCount() {
    static unsigned count = 0;
    return count;
}

inline std::string& openFailureNamespace() {
    static std::string value;
    return value;
}

inline std::string& stringReadFailureNamespace() {
    static std::string value;
    return value;
}

inline std::string& stringReadFailureKey() {
    static std::string value;
    return value;
}

inline void reset() {
    store().clear();
    writeCount() = 0;
    openFailureNamespace().clear();
    stringReadFailureNamespace().clear();
    stringReadFailureKey().clear();
}

inline bool namespaceExists(const char* ns) {
    return ns && store().find(ns) != store().end();
}

inline Value* findValue(const char* ns, const char* key) {
    if (!ns || !key) {
        return nullptr;
    }
    auto nsIt = store().find(ns);
    if (nsIt == store().end()) {
        return nullptr;
    }
    auto keyIt = nsIt->second.find(key);
    if (keyIt == nsIt->second.end()) {
        return nullptr;
    }
    return &keyIt->second;
}

}  // namespace native_nvs

class Preferences {
public:
    bool begin(const char* ns, bool readOnly = false) {
        if (!ns || ns[0] == '\0') {
            return false;
        }
        if (readOnly && !native_nvs::namespaceExists(ns)) {
            return false;
        }
        _ns = ns;
        _readOnly = readOnly;
        if (!readOnly) {
            native_nvs::store()[_ns];
        }
        return true;
    }

    void end() {
        _ns.clear();
        _readOnly = false;
    }

    bool isKey(const char* key) const {
        return native_nvs::findValue(_ns.c_str(), key) != nullptr;
    }

    int32_t getInt(const char* key, int32_t def) const {
        const native_nvs::Value* value = native_nvs::findValue(_ns.c_str(), key);
        return value && value->type == native_nvs::ValueType::Int ? value->intValue : def;
    }

    uint32_t getUInt(const char* key, uint32_t def) const {
        const native_nvs::Value* value = native_nvs::findValue(_ns.c_str(), key);
        return value && value->type == native_nvs::ValueType::UInt ? value->uintValue : def;
    }

    bool getBool(const char* key, bool def) const {
        const native_nvs::Value* value = native_nvs::findValue(_ns.c_str(), key);
        return value && value->type == native_nvs::ValueType::Bool ? value->boolValue : def;
    }

    size_t putInt(const char* key, int32_t value) {
        if (_readOnly || _ns.empty() || !key) {
            return 0;
        }
        native_nvs::Value stored;
        stored.type = native_nvs::ValueType::Int;
        stored.intValue = value;
        native_nvs::store()[_ns][key] = stored;
        ++native_nvs::writeCount();
        return sizeof(value);
    }

    size_t putUInt(const char* key, uint32_t value) {
        if (_readOnly || _ns.empty() || !key) {
            return 0;
        }
        native_nvs::Value stored;
        stored.type = native_nvs::ValueType::UInt;
        stored.uintValue = value;
        native_nvs::store()[_ns][key] = stored;
        ++native_nvs::writeCount();
        return sizeof(value);
    }

    size_t putBool(const char* key, bool value) {
        if (_readOnly || _ns.empty() || !key) {
            return 0;
        }
        native_nvs::Value stored;
        stored.type = native_nvs::ValueType::Bool;
        stored.boolValue = value;
        native_nvs::store()[_ns][key] = stored;
        ++native_nvs::writeCount();
        return 1;
    }

    size_t putString(const char* key, const char* value) {
        if (_readOnly || _ns.empty() || !key || !value) {
            return 0;
        }
        native_nvs::Value stored;
        stored.type = native_nvs::ValueType::String;
        stored.stringValue = value;
        native_nvs::store()[_ns][key] = stored;
        ++native_nvs::writeCount();
        return stored.stringValue.size();
    }

    size_t putBytes(const char* key, const void* data, size_t len) {
        if (_readOnly || _ns.empty() || !key || !data || len == 0) {
            return 0;
        }
        native_nvs::Value stored;
        stored.type = native_nvs::ValueType::Blob;
        const uint8_t* bytes = static_cast<const uint8_t*>(data);
        stored.blobValue.assign(bytes, bytes + len);
        native_nvs::store()[_ns][key] = stored;
        ++native_nvs::writeCount();
        return len;
    }

    bool clear() {
        if (_readOnly || _ns.empty()) {
            return false;
        }
        native_nvs::store()[_ns].clear();
        return true;
    }

    bool remove(const char* key) {
        if (_readOnly || _ns.empty() || !key) {
            return false;
        }
        auto nsIt = native_nvs::store().find(_ns);
        if (nsIt == native_nvs::store().end()) {
            return false;
        }
        return nsIt->second.erase(key) > 0;
    }

private:
    std::string _ns;
    bool _readOnly = false;
};
