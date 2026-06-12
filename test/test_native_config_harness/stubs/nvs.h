#pragma once

#include "Preferences.h"

#include <cstddef>
#include <cstring>

using esp_err_t = int;
using nvs_handle_t = int;

constexpr esp_err_t ESP_OK = 0;
constexpr esp_err_t ESP_ERR_NVS_NOT_FOUND = 0x1102;
constexpr esp_err_t ESP_FAIL = -1;
constexpr int NVS_READONLY = 1;

namespace native_nvs {

inline std::map<nvs_handle_t, std::string>& handles() {
    static std::map<nvs_handle_t, std::string> data;
    return data;
}

inline nvs_handle_t& nextHandle() {
    static nvs_handle_t value = 1;
    return value;
}

}  // namespace native_nvs

inline esp_err_t nvs_open(const char* ns, int, nvs_handle_t* out) {
    if (!ns || !out) {
        return ESP_FAIL;
    }
    if (!native_nvs::namespaceExists(ns)) {
        return ESP_ERR_NVS_NOT_FOUND;
    }
    const nvs_handle_t handle = native_nvs::nextHandle()++;
    native_nvs::handles()[handle] = ns;
    *out = handle;
    return ESP_OK;
}

inline void nvs_close(nvs_handle_t handle) {
    native_nvs::handles().erase(handle);
}

inline esp_err_t nvs_get_str(nvs_handle_t handle, const char* key, char* out, size_t* len) {
    if (!key || !len) {
        return ESP_FAIL;
    }
    auto handleIt = native_nvs::handles().find(handle);
    if (handleIt == native_nvs::handles().end()) {
        return ESP_FAIL;
    }
    native_nvs::Value* value = native_nvs::findValue(handleIt->second.c_str(), key);
    if (!value || value->type != native_nvs::ValueType::String) {
        return ESP_ERR_NVS_NOT_FOUND;
    }
    const size_t required = value->stringValue.size() + 1;
    if (!out) {
        *len = required;
        return ESP_OK;
    }
    if (*len < required) {
        return ESP_FAIL;
    }
    std::memcpy(out, value->stringValue.c_str(), required);
    *len = required;
    return ESP_OK;
}

inline esp_err_t nvs_get_blob(nvs_handle_t handle, const char* key, void* out, size_t* len) {
    if (!key || !len) {
        return ESP_FAIL;
    }
    auto handleIt = native_nvs::handles().find(handle);
    if (handleIt == native_nvs::handles().end()) {
        return ESP_FAIL;
    }
    native_nvs::Value* value = native_nvs::findValue(handleIt->second.c_str(), key);
    if (!value || value->type != native_nvs::ValueType::Blob) {
        return ESP_ERR_NVS_NOT_FOUND;
    }
    const size_t required = value->blobValue.size();
    if (!out) {
        *len = required;
        return ESP_OK;
    }
    if (*len < required) {
        return ESP_FAIL;
    }
    std::memcpy(out, value->blobValue.data(), required);
    *len = required;
    return ESP_OK;
}

inline esp_err_t nvs_get_i32(nvs_handle_t handle, const char* key, int32_t* out) {
    if (!key || !out) {
        return ESP_FAIL;
    }
    auto handleIt = native_nvs::handles().find(handle);
    if (handleIt == native_nvs::handles().end()) {
        return ESP_FAIL;
    }
    native_nvs::Value* value = native_nvs::findValue(handleIt->second.c_str(), key);
    if (!value || value->type != native_nvs::ValueType::Int) {
        return ESP_ERR_NVS_NOT_FOUND;
    }
    *out = value->intValue;
    return ESP_OK;
}

inline esp_err_t nvs_get_u8(nvs_handle_t handle, const char* key, uint8_t* out) {
    if (!key || !out) {
        return ESP_FAIL;
    }
    auto handleIt = native_nvs::handles().find(handle);
    if (handleIt == native_nvs::handles().end()) {
        return ESP_FAIL;
    }
    native_nvs::Value* value = native_nvs::findValue(handleIt->second.c_str(), key);
    if (!value || value->type != native_nvs::ValueType::Bool) {
        return ESP_ERR_NVS_NOT_FOUND;
    }
    *out = value->boolValue ? 1 : 0;
    return ESP_OK;
}
