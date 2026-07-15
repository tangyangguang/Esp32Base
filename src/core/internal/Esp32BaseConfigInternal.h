#pragma once

#include <stdint.h>

namespace esp32base_internal {

enum class ConfigUInt32ReadResult : uint8_t {
    Found,
    NotFound,
    Error
};

ConfigUInt32ReadResult readConfigUInt32(const char* ns, const char* key, uint32_t& value);
bool writeConfigUInt32(const char* ns, const char* key, uint32_t value);

} // namespace esp32base_internal
