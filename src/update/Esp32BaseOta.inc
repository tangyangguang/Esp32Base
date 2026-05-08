#include "../Esp32BaseProfile.h"

#if ESP32BASE_ENABLE_OTA

#include "Esp32BaseOta.h"
#include "../core/Esp32BaseConfig.h"
#include "../core/Esp32BaseLog.h"
#include "../core/Esp32BaseSystem.h"

#if ESP32BASE_ENABLE_FILELOG
#include "../runtime/Esp32BaseFileLog.h"
#endif
#if ESP32BASE_ENABLE_BUS
#include "../runtime/Esp32BaseBus.h"
#endif
#if ESP32BASE_ENABLE_WIFI
#include "../network/Esp32BaseWiFi.h"
#endif
#if ESP32BASE_ENABLE_WATCHDOG
#include "../runtime/Esp32BaseWatchdog.h"
#endif

#include <Update.h>
#include <esp_ota_ops.h>
#include <mbedtls/sha256.h>
#if __has_include(<mbedtls/version.h>)
#include <mbedtls/version.h>
#endif
#include <ctype.h>
#include <string.h>

#ifndef ESP32BASE_OTA_REQUIRE_MARK_VALID
#define ESP32BASE_OTA_REQUIRE_MARK_VALID 0
#endif

#ifndef ESP32BASE_OTA_MARK_VALID_TIMEOUT_MS
#define ESP32BASE_OTA_MARK_VALID_TIMEOUT_MS 30000
#endif

namespace {
bool g_otaReady = false;
Esp32BaseOta::Status g_otaStatus = Esp32BaseOta::IDLE;
size_t g_otaTotal = 0;
size_t g_otaProcessed = 0;
char g_otaExpected[65] = "";
char g_otaLastError[96] = "";
mbedtls_sha256_context g_otaSha;
bool g_otaShaActive = false;
bool g_otaPrevPowerSave = true;
uint32_t g_otaBootMs = 0;
const esp_partition_t* g_otaBootPartition = nullptr;

void setError(const char* message) {
    strlcpy(g_otaLastError, message ? message : "", sizeof(g_otaLastError));
    ESP32BASE_LOG_E("ota", "%s", g_otaLastError);
#if ESP32BASE_ENABLE_BUS
    Esp32BaseBus::publish(Esp32BaseOta::EVENT_FAILED, g_otaLastError);
#endif
}

void bytesToHex(const uint8_t* bytes, size_t len, char* out, size_t outLen) {
    static const char* hex = "0123456789abcdef";
    if (!out || outLen < len * 2 + 1) {
        return;
    }
    for (size_t i = 0; i < len; ++i) {
        out[i * 2] = hex[(bytes[i] >> 4) & 0x0f];
        out[i * 2 + 1] = hex[bytes[i] & 0x0f];
    }
    out[len * 2] = '\0';
}

bool copySha256Hex(const char* in, char* out, size_t outLen) {
    if (!in || !out || outLen < 65 || strlen(in) != 64) {
        return false;
    }
    for (size_t i = 0; i < 64; ++i) {
        const unsigned char c = static_cast<unsigned char>(in[i]);
        if (!isxdigit(c)) {
            return false;
        }
        out[i] = static_cast<char>(tolower(c));
    }
    out[64] = '\0';
    return true;
}

void cleanupLongOps() {
#if ESP32BASE_ENABLE_WATCHDOG
    Esp32BaseWatchdog::restoreCurrentTaskAfterLongOperation();
#endif
#if ESP32BASE_ENABLE_WIFI
    Esp32BaseWiFi::setPowerSave(g_otaPrevPowerSave);
#endif
    Esp32BaseConfig::resumeDeferredFlush();
}

void sha256Starts(mbedtls_sha256_context* ctx) {
#if defined(MBEDTLS_VERSION_MAJOR) && MBEDTLS_VERSION_MAJOR >= 3 && defined(MBEDTLS_SHA256_SHA256)
    mbedtls_sha256_starts(ctx, MBEDTLS_SHA256_SHA256);
#else
    mbedtls_sha256_starts(ctx, 0);
#endif
}
}

bool Esp32BaseOta::begin() {
    g_otaReady = true;
    g_otaStatus = READY;
    g_otaBootMs = millis();
    g_otaBootPartition = esp_ota_get_running_partition();
    if (g_otaBootPartition) {
        ESP32BASE_LOG_D("ota", "ready partition=%s address=0x%lx size=%lu",
                        g_otaBootPartition->label,
                        static_cast<unsigned long>(g_otaBootPartition->address),
                        static_cast<unsigned long>(g_otaBootPartition->size));
    } else {
        ESP32BASE_LOG_D("ota", "ready partition=unknown");
    }
    return true;
}

void Esp32BaseOta::handle() {
#if ESP32BASE_OTA_REQUIRE_MARK_VALID
    if (!g_otaReady || !g_otaBootPartition || esp_ota_get_running_partition() != g_otaBootPartition) {
        return;
    }
    if (millis() - g_otaBootMs >= ESP32BASE_OTA_MARK_VALID_TIMEOUT_MS) {
        esp_ota_img_states_t state;
        if (esp_ota_get_state_partition(g_otaBootPartition, &state) == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY) {
            ESP32BASE_LOG_E("ota", "mark-valid timeout, rollback");
            rollbackAndRestart("ota mark-valid timeout");
        }
    }
#endif
}

bool Esp32BaseOta::isReady() {
    return g_otaReady;
}

bool Esp32BaseOta::isUploading() {
    return g_otaStatus == UPLOADING || g_otaStatus == VERIFYING;
}

Esp32BaseOta::Status Esp32BaseOta::status() {
    return g_otaStatus;
}

bool Esp32BaseOta::startUpload(size_t totalSize, const char* expectedSha256Hex) {
    if (totalSize == 0) {
        setError("invalid ota size");
        g_otaStatus = FAILED;
        return false;
    }
    g_otaTotal = totalSize;
    g_otaProcessed = 0;
    g_otaExpected[0] = '\0';
    g_otaLastError[0] = '\0';
    if (expectedSha256Hex && expectedSha256Hex[0]) {
        if (!copySha256Hex(expectedSha256Hex, g_otaExpected, sizeof(g_otaExpected))) {
            setError("invalid sha256 hex");
            g_otaStatus = FAILED;
            return false;
        }
    }

    Esp32BaseConfig::pauseDeferredFlush();
#if ESP32BASE_ENABLE_WATCHDOG
    Esp32BaseWatchdog::removeCurrentTaskForLongOperation();
#endif
#if ESP32BASE_ENABLE_WIFI
    g_otaPrevPowerSave = Esp32BaseWiFi::powerSave();
    Esp32BaseWiFi::setPowerSave(false);
#endif

    if (!Update.begin(totalSize)) {
        cleanupLongOps();
        setError("Update.begin failed");
        g_otaStatus = FAILED;
        return false;
    }

    mbedtls_sha256_init(&g_otaSha);
    sha256Starts(&g_otaSha);
    g_otaShaActive = true;
    g_otaStatus = UPLOADING;
#if ESP32BASE_ENABLE_BUS
    Esp32BaseBus::publish(EVENT_START, "");
#endif
    return true;
}

bool Esp32BaseOta::writeChunk(const uint8_t* data, size_t len) {
    if (g_otaStatus != UPLOADING || !data) {
        return false;
    }
    if (Update.write(const_cast<uint8_t*>(data), len) != len) {
        abortUpload("Update.write failed");
        return false;
    }
    if (g_otaShaActive) {
        mbedtls_sha256_update(&g_otaSha, data, len);
    }
    g_otaProcessed += len;
    yield();
#if ESP32BASE_ENABLE_BUS
    Esp32BaseBus::publish(EVENT_PROGRESS, "");
#endif
    return true;
}

bool Esp32BaseOta::finishUpload() {
    if (g_otaStatus != UPLOADING) {
        return false;
    }
    g_otaStatus = VERIFYING;

    uint8_t digest[32];
    char actual[65];
    if (g_otaShaActive) {
        mbedtls_sha256_finish(&g_otaSha, digest);
        mbedtls_sha256_free(&g_otaSha);
        g_otaShaActive = false;
        bytesToHex(digest, sizeof(digest), actual, sizeof(actual));
    } else {
        actual[0] = '\0';
    }

    if (g_otaExpected[0] && strcmp(g_otaExpected, actual) != 0) {
        Update.abort();
        cleanupLongOps();
        setError("sha256 mismatch");
        g_otaStatus = FAILED;
        return false;
    }

    if (g_otaProcessed != g_otaTotal) {
        Update.abort();
        cleanupLongOps();
        setError("ota size mismatch");
        g_otaStatus = FAILED;
        return false;
    }

    if (!Update.end(true)) {
        cleanupLongOps();
        setError("Update.end failed");
        g_otaStatus = FAILED;
        return false;
    }

    cleanupLongOps();
    g_otaStatus = SUCCESS;
    ESP32BASE_LOG_I("ota", "upload success bytes=%lu", static_cast<unsigned long>(g_otaProcessed));
#if ESP32BASE_ENABLE_FILELOG
    Esp32BaseFileLog::flush();
#endif
#if ESP32BASE_ENABLE_BUS
    Esp32BaseBus::publish(EVENT_SUCCESS, "");
#endif
    return true;
}

void Esp32BaseOta::abortUpload(const char* reason) {
    if (g_otaShaActive) {
        mbedtls_sha256_free(&g_otaSha);
        g_otaShaActive = false;
    }
    Update.abort();
    cleanupLongOps();
    setError(reason ? reason : "ota aborted");
    g_otaStatus = FAILED;
}

uint8_t Esp32BaseOta::progress() {
    if (g_otaTotal == 0) {
        return 0;
    }
    const size_t percent = (g_otaProcessed * 100U) / g_otaTotal;
    return percent > 100U ? 100U : static_cast<uint8_t>(percent);
}

size_t Esp32BaseOta::bytesProcessed() {
    return g_otaProcessed;
}

size_t Esp32BaseOta::totalSize() {
    return g_otaTotal;
}

const char* Esp32BaseOta::lastError() {
    return g_otaLastError;
}

bool Esp32BaseOta::markCurrentValid() {
    return esp_ota_mark_app_valid_cancel_rollback() == ESP_OK;
}

bool Esp32BaseOta::isRollbackPossible() {
    return esp_ota_check_rollback_is_possible();
}

void Esp32BaseOta::rollbackAndRestart(const char* reason) {
    const char* rollbackReason = reason ? reason : "ota rollback";
    Esp32BaseSystem::appendRestartLog(rollbackReason);
    Esp32BaseConfig::flushAll();
#if ESP32BASE_ENABLE_FILELOG
    Esp32BaseFileLog::flush();
#endif
    ESP32BASE_LOG_E("ota", "rollback restart: %s", rollbackReason);
#if ESP32BASE_ENABLE_FILELOG
    Esp32BaseFileLog::flush();
#endif
    delay(50);
    if (esp_ota_mark_app_invalid_rollback_and_reboot() != ESP_OK) {
        Esp32BaseSystem::restart(rollbackReason);
    }
}

#endif
