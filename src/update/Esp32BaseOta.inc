#include "../Esp32BaseProfile.h"

#if ESP32BASE_ENABLE_OTA

#include "Esp32BaseOta.h"
#include "../Esp32Base.h"
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
#if ESP32BASE_ENABLE_ARDUINO_OTA
#include "../web/Esp32BaseWeb.h"
#include <ArduinoOTA.h>
#endif

#include <Update.h>
#include <esp_ota_ops.h>
#include <mbedtls/sha256.h>
#if __has_include(<mbedtls/version.h>)
#include <mbedtls/version.h>
#endif
#include <ctype.h>
#include <new>
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
uint8_t g_otaNextProgressLog = 10;
char g_otaExpected[65] = "";
char g_otaLastError[96] = "";
mbedtls_sha256_context g_otaSha;
bool g_otaShaActive = false;
bool g_otaPrevPowerSave = true;
uint32_t g_otaTransferStartedMs = 0;
uint32_t g_otaTransferFinishedMs = 0;
bool g_otaSummaryLogged = false;
uint32_t g_otaBootMs = 0;
const esp_partition_t* g_otaBootPartition = nullptr;
#if ESP32BASE_ENABLE_ARDUINO_OTA
bool g_arduinoOtaReady = false;
bool g_arduinoOtaTransferActive = false;
bool g_arduinoOtaRestartPending = false;
char g_arduinoOtaAuthPass[64] = "";
#endif

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

void prepareLongOps() {
    Esp32BaseConfig::pauseDeferredFlush();
#if ESP32BASE_ENABLE_WATCHDOG
    Esp32BaseWatchdog::removeCurrentTaskForLongOperation();
#endif
#if ESP32BASE_ENABLE_WIFI
    g_otaPrevPowerSave = Esp32BaseWiFi::powerSave();
    Esp32BaseWiFi::setPowerSave(false);
#endif
}

void sha256Starts(mbedtls_sha256_context* ctx) {
#if defined(MBEDTLS_VERSION_MAJOR) && MBEDTLS_VERSION_MAJOR >= 3 && defined(MBEDTLS_SHA256_SHA256)
    mbedtls_sha256_starts(ctx, MBEDTLS_SHA256_SHA256);
#else
    mbedtls_sha256_starts(ctx, 0);
#endif
}

void formatOtaBytes(size_t bytes, char* out, size_t len) {
    if (!out || len == 0) {
        return;
    }
    if (bytes >= 1024UL * 1024UL) {
        snprintf(out, len, "%.2f MB", static_cast<double>(bytes) / 1024.0 / 1024.0);
    } else if (bytes >= 1024UL) {
        snprintf(out, len, "%.2f KB", static_cast<double>(bytes) / 1024.0);
    } else {
        snprintf(out, len, "%lu B", static_cast<unsigned long>(bytes));
    }
}

void formatOtaBytesShort(double bytes, char* out, size_t len) {
    if (!out || len == 0) {
        return;
    }
    if (bytes >= 1024.0 * 1024.0) {
        snprintf(out, len, "%.1f MB", bytes / 1024.0 / 1024.0);
    } else if (bytes >= 1024.0) {
        snprintf(out, len, "%.1f KB", bytes / 1024.0);
    } else {
        snprintf(out, len, "%.0f B", bytes);
    }
}

void resetOtaTransferStats() {
    g_otaTransferStartedMs = millis();
    g_otaTransferFinishedMs = 0;
    g_otaSummaryLogged = false;
}

void finishOtaTransferStats() {
    if (g_otaTransferStartedMs != 0 && g_otaTransferFinishedMs == 0) {
        g_otaTransferFinishedMs = millis();
    }
}

void logOtaTransferSummary(const char* prefix) {
    if (g_otaSummaryLogged || g_otaTransferStartedMs == 0) {
        return;
    }
    g_otaSummaryLogged = true;
    finishOtaTransferStats();
    const uint32_t durationMs = g_otaTransferFinishedMs - g_otaTransferStartedMs;
    const double durationSec = static_cast<double>(durationMs) / 1000.0;
    char uploadedBuf[32];
    char speedBuf[40];
    formatOtaBytesShort(static_cast<double>(g_otaProcessed), uploadedBuf, sizeof(uploadedBuf));
    if (durationSec >= 0.1) {
        char speedBytes[32];
        formatOtaBytesShort(static_cast<double>(g_otaProcessed) / durationSec, speedBytes, sizeof(speedBytes));
        snprintf(speedBuf, sizeof(speedBuf), "%s/s", speedBytes);
    } else {
        strlcpy(speedBuf, "n/a", sizeof(speedBuf));
    }
    ESP32BASE_LOG_I("ota", "%s duration=%.2fs uploaded=%s average=%s",
                    prefix && prefix[0] ? prefix : "upload_summary",
                    durationSec,
                    uploadedBuf,
                    speedBuf);
}

void logProgressIfDue() {
    const uint8_t currentProgress = Esp32BaseOta::progress();
    while (g_otaNextProgressLog <= 100U && currentProgress >= g_otaNextProgressLog) {
        char processedBuf[32];
        char totalBuf[32];
        formatOtaBytes(g_otaProcessed, processedBuf, sizeof(processedBuf));
        formatOtaBytes(g_otaTotal, totalBuf, sizeof(totalBuf));
        ESP32BASE_LOG_I("ota", "upload_progress progress=%u%% bytes=%s total=%s",
                        static_cast<unsigned>(g_otaNextProgressLog),
                        processedBuf,
                        totalBuf);
        g_otaNextProgressLog = static_cast<uint8_t>(g_otaNextProgressLog + 10U);
    }
}

#if ESP32BASE_ENABLE_ARDUINO_OTA
const char* arduinoOtaErrorName(ota_error_t error) {
    switch (error) {
        case OTA_AUTH_ERROR: return "auth";
        case OTA_BEGIN_ERROR: return "begin";
        case OTA_CONNECT_ERROR: return "connect";
        case OTA_RECEIVE_ERROR: return "receive";
        case OTA_END_ERROR: return "end";
        default: return "unknown";
    }
}

void resetArduinoOtaObject(bool stopActiveListener) {
    if (stopActiveListener) {
        ArduinoOTA.end();
    }
    ArduinoOTA.~ArduinoOTAClass();
    new (&ArduinoOTA) ArduinoOTAClass();
}

void configureArduinoOtaCallbacks() {
    ArduinoOTA.onStart([]() {
        g_arduinoOtaTransferActive = true;
        g_arduinoOtaRestartPending = false;
        g_otaProcessed = 0;
        g_otaNextProgressLog = 10;
        g_otaLastError[0] = '\0';
        resetOtaTransferStats();
        g_otaStatus = Esp32BaseOta::UPLOADING;
        prepareLongOps();
        char totalBuf[32];
        formatOtaBytes(g_otaTotal, totalBuf, sizeof(totalBuf));
        ESP32BASE_LOG_I("ota", "arduino_upload_start size=%s auth=enabled md5=arduinoota",
                        g_otaTotal ? totalBuf : "unknown");
#if ESP32BASE_ENABLE_BUS
        Esp32BaseBus::publish(Esp32BaseOta::EVENT_START, "");
#endif
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        g_otaTotal = static_cast<size_t>(total);
        g_otaProcessed = static_cast<size_t>(progress);
        logProgressIfDue();
#if ESP32BASE_ENABLE_BUS
        Esp32BaseBus::publish(Esp32BaseOta::EVENT_PROGRESS, "");
#endif
    });
    ArduinoOTA.onEnd([]() {
        g_otaStatus = Esp32BaseOta::SUCCESS;
        g_arduinoOtaTransferActive = false;
        cleanupLongOps();
        char processedBuf[32];
        formatOtaBytes(g_otaProcessed, processedBuf, sizeof(processedBuf));
        ESP32BASE_LOG_I("ota", "arduino_upload_success size=%s", processedBuf);
        logOtaTransferSummary("arduino_upload_summary");
#if ESP32BASE_ENABLE_FILELOG
        Esp32BaseFileLog::flush();
#endif
#if ESP32BASE_ENABLE_BUS
        Esp32BaseBus::publish(Esp32BaseOta::EVENT_SUCCESS, "");
#endif
        g_arduinoOtaRestartPending = true;
    });
    ArduinoOTA.onError([](ota_error_t error) {
        const bool wasActive = g_arduinoOtaTransferActive;
        g_arduinoOtaTransferActive = false;
        if (wasActive) {
            cleanupLongOps();
        }
        char message[96];
        snprintf(message, sizeof(message), "arduino ota %s error", arduinoOtaErrorName(error));
        setError(message);
        logOtaTransferSummary("arduino_upload_failed");
        g_otaStatus = Esp32BaseOta::FAILED;
    });
}

void beginArduinoOta() {
    if (g_arduinoOtaReady) {
        return;
    }
    strlcpy(g_arduinoOtaAuthPass, Esp32BaseWeb::authPassword(), sizeof(g_arduinoOtaAuthPass));
    resetArduinoOtaObject(false);
    ArduinoOTA.setHostname(Esp32Base::hostname());
    ArduinoOTA.setPassword(g_arduinoOtaAuthPass);
    ArduinoOTA.setMdnsEnabled(false);
    ArduinoOTA.setRebootOnSuccess(false);
    configureArduinoOtaCallbacks();
    ArduinoOTA.begin();
    g_arduinoOtaReady = true;
    ESP32BASE_LOG_I("ota", "arduino_ota_ready host=%s.local port=3232 auth=enabled",
                    Esp32Base::hostname());
}

void syncArduinoOtaAuth() {
    if (!g_arduinoOtaReady || g_arduinoOtaTransferActive || g_otaStatus == Esp32BaseOta::UPLOADING ||
        g_otaStatus == Esp32BaseOta::VERIFYING) {
        return;
    }
    const char* currentPass = Esp32BaseWeb::authPassword();
    if (strcmp(g_arduinoOtaAuthPass, currentPass) == 0) {
        return;
    }
    ESP32BASE_LOG_I("ota", "arduino_ota_auth_changed restarting_listener");
    resetArduinoOtaObject(true);
    g_arduinoOtaReady = false;
    beginArduinoOta();
}
#endif
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
#if ESP32BASE_ENABLE_ARDUINO_OTA
    beginArduinoOta();
#endif
    return true;
}

void Esp32BaseOta::handle() {
#if ESP32BASE_ENABLE_ARDUINO_OTA
    if (g_arduinoOtaRestartPending) {
        g_arduinoOtaRestartPending = false;
        delay(500);
        Esp32BaseSystem::restart("arduino ota success");
        return;
    }
    if (g_otaReady) {
        if (!g_arduinoOtaReady) {
            beginArduinoOta();
        } else {
            syncArduinoOtaAuth();
        }
#if ESP32BASE_ENABLE_WIFI
        if (Esp32BaseWiFi::isConnected() && g_arduinoOtaReady &&
            (!isUploading() || g_arduinoOtaTransferActive)) {
            ArduinoOTA.handle();
        }
#endif
    }
#endif
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
    if (isUploading()) {
        setError("ota already in progress");
        return false;
    }
    if (totalSize == 0) {
        setError("invalid ota size");
        g_otaStatus = FAILED;
        return false;
    }
    const esp_partition_t* nextPartition = esp_ota_get_next_update_partition(nullptr);
    if (!nextPartition) {
        setError("no ota update partition");
        g_otaStatus = FAILED;
        return false;
    }
    if (totalSize > nextPartition->size) {
        char totalBuf[32];
        char partitionBuf[32];
        char message[96];
        formatOtaBytes(totalSize, totalBuf, sizeof(totalBuf));
        formatOtaBytes(nextPartition->size, partitionBuf, sizeof(partitionBuf));
        snprintf(message, sizeof(message), "ota size too large: firmware=%s partition=%s", totalBuf, partitionBuf);
        setError(message);
        g_otaStatus = FAILED;
        return false;
    }
    g_otaTotal = totalSize;
    g_otaProcessed = 0;
    g_otaNextProgressLog = 10;
    g_otaExpected[0] = '\0';
    g_otaLastError[0] = '\0';
    resetOtaTransferStats();
    if (expectedSha256Hex && expectedSha256Hex[0]) {
        if (!copySha256Hex(expectedSha256Hex, g_otaExpected, sizeof(g_otaExpected))) {
            setError("invalid sha256 hex");
            logOtaTransferSummary("upload_failed");
            g_otaStatus = FAILED;
            return false;
        }
    }

    prepareLongOps();

    if (!Update.begin(totalSize)) {
        cleanupLongOps();
        char message[96];
        snprintf(message, sizeof(message), "Update.begin failed: %s", Update.errorString());
        setError(message);
        logOtaTransferSummary("upload_failed");
        g_otaStatus = FAILED;
        return false;
    }

    mbedtls_sha256_init(&g_otaSha);
    sha256Starts(&g_otaSha);
    g_otaShaActive = true;
    g_otaStatus = UPLOADING;
    char totalBuf[32];
    formatOtaBytes(g_otaTotal, totalBuf, sizeof(totalBuf));
    ESP32BASE_LOG_I("ota", "upload_start size=%s sha256=%s",
                    totalBuf,
                    g_otaExpected[0] ? "provided" : "none");
#if ESP32BASE_ENABLE_BUS
    Esp32BaseBus::publish(EVENT_START, "");
#endif
    return true;
}

bool Esp32BaseOta::writeChunk(const uint8_t* data, size_t len) {
    if (g_otaStatus != UPLOADING || !data) {
        return false;
    }
    if (g_otaProcessed > g_otaTotal || len > g_otaTotal - g_otaProcessed) {
        abortUpload("ota size overflow");
        return false;
    }
    if (Update.write(const_cast<uint8_t*>(data), len) != len) {
        finishOtaTransferStats();
        abortUpload("Update.write failed");
        return false;
    }
    if (g_otaShaActive) {
        mbedtls_sha256_update(&g_otaSha, data, len);
    }
    g_otaProcessed += len;
    logProgressIfDue();
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
        finishOtaTransferStats();
        setError("sha256 mismatch");
        logOtaTransferSummary("upload_failed");
        g_otaStatus = FAILED;
        return false;
    }

    if (g_otaProcessed != g_otaTotal) {
        Update.abort();
        cleanupLongOps();
        finishOtaTransferStats();
        setError("ota size mismatch");
        logOtaTransferSummary("upload_failed");
        g_otaStatus = FAILED;
        return false;
    }

    if (!Update.end(true)) {
        cleanupLongOps();
        finishOtaTransferStats();
        setError("Update.end failed");
        logOtaTransferSummary("upload_failed");
        g_otaStatus = FAILED;
        return false;
    }

    cleanupLongOps();
    finishOtaTransferStats();
    g_otaStatus = SUCCESS;
    char processedBuf[32];
    formatOtaBytes(g_otaProcessed, processedBuf, sizeof(processedBuf));
    ESP32BASE_LOG_I("ota", "upload_success size=%s sha256=%s",
                    processedBuf,
                    actual[0] ? actual : "unavailable");
    logOtaTransferSummary("upload_summary");
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
    finishOtaTransferStats();
    setError(reason ? reason : "ota aborted");
    logOtaTransferSummary("upload_aborted");
    g_otaStatus = FAILED;
}

uint8_t Esp32BaseOta::progress() {
    if (g_otaTotal == 0) {
        return 0;
    }
    const uint64_t percent = (static_cast<uint64_t>(g_otaProcessed) * 100ULL) / g_otaTotal;
    return percent > 100U ? 100U : static_cast<uint8_t>(percent);
}

size_t Esp32BaseOta::bytesProcessed() {
    return g_otaProcessed;
}

size_t Esp32BaseOta::totalSize() {
    return g_otaTotal;
}

uint32_t Esp32BaseOta::elapsedMs() {
    if (g_otaTransferStartedMs == 0) {
        return 0;
    }
    if (g_otaTransferFinishedMs != 0) {
        return g_otaTransferFinishedMs - g_otaTransferStartedMs;
    }
    return millis() - g_otaTransferStartedMs;
}

uint32_t Esp32BaseOta::averageBytesPerSecond() {
    const uint32_t elapsed = elapsedMs();
    if (elapsed == 0) {
        return 0;
    }
    const uint64_t bytesPerSecond = (static_cast<uint64_t>(g_otaProcessed) * 1000ULL) / elapsed;
    return bytesPerSecond > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(bytesPerSecond);
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
