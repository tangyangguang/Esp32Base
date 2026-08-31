#pragma once

#include <esp_idf_version.h>
#include <esp_ota_ops.h>

#if ESP_IDF_VERSION_MAJOR >= 5
#include <esp_app_desc.h>
#endif

namespace esp32base_internal {

inline int appElfSha256(char* out, size_t len) {
#if ESP_IDF_VERSION_MAJOR >= 5
    return esp_app_get_elf_sha256(out, len);
#else
    return esp_ota_get_app_elf_sha256(out, len);
#endif
}

} // namespace esp32base_internal
