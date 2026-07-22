#pragma once

#include <stdint.h>

namespace esp32base_internal {

enum class ConfigUInt32ReadResult : uint8_t {
    Found,
    NotFound,
    Error
};

enum class ConfigKeyRemoveResult : uint8_t {
    Removed,
    NotFound,
    Error
};

ConfigUInt32ReadResult readConfigUInt32(const char* ns, const char* key, uint32_t& value);
bool writeConfigUInt32(const char* ns, const char* key, uint32_t value);
ConfigKeyRemoveResult removeConfigKey(const char* ns, const char* key);

} // namespace esp32base_internal
