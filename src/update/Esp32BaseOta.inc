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
#include "../runtime/internal/Esp32BaseLongOperation.h"
#if ESP32BASE_ENABLE_ARDUINO_OTA
#include "../web/Esp32BaseWeb.h"
#include <ArduinoOTA.h>
#endif

#include <Update.h>
#include <esp_err.h>
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

#if ESP32BASE_OTA_REQUIRE_MARK_VALID
extern "C" bool verifyRollbackLater() {
    return true;
}
#endif

namespace {
bool g_otaReady = false;
bool g_otaNetworkServicesReady = false;
Esp32BaseOta::Status g_otaStatus = Esp32BaseOta::IDLE;
size_t g_otaTotal = 0;
size_t g_otaProcessed = 0;
uint8_t g_otaNextProgressLog = 10;
char g_otaExpected[65] = "";
char g_otaCalculated[65] = "";
char g_otaLastError[160] = "";
mbedtls_sha256_context g_otaSha;
bool g_otaShaActive = false;
bool g_otaPrevPowerSave = true;
bool g_otaLongOpsActive = false;
uint32_t g_otaTransferStartedMs = 0;
uint32_t g_otaTransferFinishedMs = 0;
bool g_otaSummaryLogged = false;
uint32_t g_otaBootMs = 0;
const esp_partition_t* g_otaBootPartition = nullptr;
bool g_otaWaitingForMarkValidLogged = false;
bool g_otaMarkValidTimeoutHandled = false;
const esp_partition_t* g_otaTargetPartition = nullptr;
char g_otaTargetPartitionLabel[17] = "";
uint32_t g_otaTargetPartitionAddress = 0;
size_t g_otaTargetPartitionSize = 0;
char g_otaLastBootPartitionLabel[17] = "";
uint32_t g_otaLastBootPartitionAddress = 0;
size_t g_otaLastBootPartitionSize = 0;
char g_otaStage[17] = "idle";
#if ESP32BASE_ENABLE_ARDUINO_OTA
bool g_arduinoOtaReady = false;
bool g_arduinoOtaTransferActive = false;
bool g_arduinoOtaRestartPending = false;
char g_arduinoOtaAuthPass[64] = "";
#endif

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

bool sameOtaPartition(const esp_partition_t* a, const esp_partition_t* b) {
    return a && b && a->address == b->address && a->size == b->size &&
           a->type == b->type && a->subtype == b->subtype &&
           strncmp(a->label, b->label, sizeof(a->label)) == 0;
}

void storePartition(const esp_partition_t* partition, char* label, size_t labelLen, uint32_t& address, size_t& size) {
    if (label && labelLen) {
        label[0] = '\0';
    }
    address = 0;
    size = 0;
    if (!partition) {
        return;
    }
    if (label && labelLen) {
        strlcpy(label, partition->label, labelLen);
    }
    address = partition->address;
    size = partition->size;
}

const char* partitionLabelOrUnknown(const esp_partition_t* partition) {
    return partition && partition->label[0] ? partition->label : "unknown";
}

uint32_t partitionAddressOrZero(const esp_partition_t* partition) {
    return partition ? partition->address : 0;
}

const char* otaStateName(esp_ota_img_states_t state) {
    switch (state) {
        case ESP_OTA_IMG_NEW: return "new";
        case ESP_OTA_IMG_PENDING_VERIFY: return "pending_verify";
        case ESP_OTA_IMG_VALID: return "valid";
        case ESP_OTA_IMG_INVALID: return "invalid";
        case ESP_OTA_IMG_ABORTED: return "aborted";
        case ESP_OTA_IMG_UNDEFINED: return "undefined";
        default: return "unknown";
    }
}

const char* partitionOtaStateName(const esp_partition_t* partition) {
    if (!partition || partition->type != ESP_PARTITION_TYPE_APP ||
        partition->subtype < ESP_PARTITION_SUBTYPE_APP_OTA_MIN ||
        partition->subtype > ESP_PARTITION_SUBTYPE_APP_OTA_MAX) {
        return "n/a";
    }
    esp_ota_img_states_t state;
    const esp_err_t err = esp_ota_get_state_partition(partition, &state);
    if (err == ESP_ERR_NOT_FOUND) {
        return "undefined";
    }
    if (err != ESP_OK) {
        return "unknown";
    }
    return otaStateName(state);
}

bool currentRunningOtaState(esp_ota_img_states_t& state, esp_err_t& err) {
    const esp_partition_t* running = esp_ota_get_running_partition();
    state = ESP_OTA_IMG_UNDEFINED;
    err = running ? esp_ota_get_state_partition(running, &state) : ESP_ERR_NOT_FOUND;
    return err == ESP_OK;
}

void formatOtaBytes(size_t bytes, char* out, size_t len);

void setOtaStage(const char* stage) {
    strlcpy(g_otaStage, stage && stage[0] ? stage : "unknown", sizeof(g_otaStage));
}

void setError(const char* message) {
    strlcpy(g_otaLastError, message ? message : "", sizeof(g_otaLastError));
    char processedBuf[32];
    char totalBuf[32];
    formatOtaBytes(g_otaProcessed, processedBuf, sizeof(processedBuf));
    formatOtaBytes(g_otaTotal, totalBuf, sizeof(totalBuf));
    ESP32BASE_LOG_E("ota", "stage=%s error=%s target=%s address=0x%lx bytes=%s total=%s progress=%u%%",
                    g_otaStage[0] ? g_otaStage : "unknown",
                    g_otaLastError,
                    g_otaTargetPartitionLabel[0] ? g_otaTargetPartitionLabel : "unknown",
                    static_cast<unsigned long>(g_otaTargetPartitionAddress),
                    processedBuf,
                    totalBuf,
                    static_cast<unsigned>(Esp32BaseOta::progress()));
#if ESP32BASE_ENABLE_BUS
    Esp32BaseBus::publish(Esp32BaseOta::EVENT_FAILED, g_otaLastError);
#endif
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
    if (!g_otaLongOpsActive) {
        return;
    }
#if ESP32BASE_ENABLE_WATCHDOG
    Esp32BaseWatchdog::exitLongOperation();
#endif
#if ESP32BASE_ENABLE_WIFI
    Esp32BaseWiFi::setPowerSave(g_otaPrevPowerSave);
#endif
    Esp32BaseConfig::resumeDeferredFlush();
    g_otaLongOpsActive = false;
}

void prepareLongOps() {
    g_otaLongOpsActive = true;
    Esp32BaseConfig::pauseDeferredFlush();
#if ESP32BASE_ENABLE_WATCHDOG
    Esp32BaseWatchdog::enterLongOperation();
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
        setOtaStage("write");
        ESP32BASE_LOG_I("ota", "upload_progress stage=write progress=%u%% bytes=%s total=%s target=%s address=0x%lx",
                        static_cast<unsigned>(g_otaNextProgressLog),
                        processedBuf,
                        totalBuf,
                        g_otaTargetPartitionLabel[0] ? g_otaTargetPartitionLabel : "unknown",
                        static_cast<unsigned long>(g_otaTargetPartitionAddress));
        g_otaNextProgressLog = static_cast<uint8_t>(g_otaNextProgressLog + 10U);
    }
}

void logUploadStage(const char* stage) {
    setOtaStage(stage);
    char processedBuf[32];
    char totalBuf[32];
    formatOtaBytes(g_otaProcessed, processedBuf, sizeof(processedBuf));
    formatOtaBytes(g_otaTotal, totalBuf, sizeof(totalBuf));
    ESP32BASE_LOG_I("ota", "upload_stage stage=%s target=%s address=0x%lx progress=%u%% bytes=%s total=%s",
                    g_otaStage,
                    g_otaTargetPartitionLabel[0] ? g_otaTargetPartitionLabel : "unknown",
                    static_cast<unsigned long>(g_otaTargetPartitionAddress),
                    static_cast<unsigned>(Esp32BaseOta::progress()),
                    processedBuf,
                    totalBuf);
}

void checkMarkValidTimeout() {
#if ESP32BASE_OTA_REQUIRE_MARK_VALID
    if (!g_otaReady || !g_otaBootPartition || esp_ota_get_running_partition() != g_otaBootPartition ||
        g_otaMarkValidTimeoutHandled) {
        return;
    }

    esp_ota_img_states_t state;
    const esp_err_t err = esp_ota_get_state_partition(g_otaBootPartition, &state);
    if (err != ESP_OK || state != ESP_OTA_IMG_PENDING_VERIFY) {
        return;
    }

    if (!g_otaWaitingForMarkValidLogged) {
        g_otaWaitingForMarkValidLogged = true;
        ESP32BASE_LOG_W("ota",
                        "waiting_for_mark_valid partition=%s address=0x%lx state=%s elapsed_ms=%lu timeout_ms=%lu",
                        partitionLabelOrUnknown(g_otaBootPartition),
                        static_cast<unsigned long>(partitionAddressOrZero(g_otaBootPartition)),
                        otaStateName(state),
                        static_cast<unsigned long>(millis() - g_otaBootMs),
                        static_cast<unsigned long>(ESP32BASE_OTA_MARK_VALID_TIMEOUT_MS));
    }

    if (millis() - g_otaBootMs >= ESP32BASE_OTA_MARK_VALID_TIMEOUT_MS) {
        g_otaMarkValidTimeoutHandled = true;
        ESP32BASE_LOG_E("ota",
                        "mark_valid_timeout partition=%s address=0x%lx state=%s elapsed_ms=%lu timeout_ms=%lu",
                        partitionLabelOrUnknown(g_otaBootPartition),
                        static_cast<unsigned long>(partitionAddressOrZero(g_otaBootPartition)),
                        otaStateName(state),
                        static_cast<unsigned long>(millis() - g_otaBootMs),
                        static_cast<unsigned long>(ESP32BASE_OTA_MARK_VALID_TIMEOUT_MS));
        Esp32BaseOta::rollbackAndRestart("ota mark-valid timeout");
    }
#endif
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
        g_otaCalculated[0] = '\0';
        setOtaStage("begin");
        g_otaTargetPartition = esp_ota_get_next_update_partition(nullptr);
        storePartition(g_otaTargetPartition,
                       g_otaTargetPartitionLabel,
                       sizeof(g_otaTargetPartitionLabel),
                       g_otaTargetPartitionAddress,
                       g_otaTargetPartitionSize);
        storePartition(nullptr,
                       g_otaLastBootPartitionLabel,
                       sizeof(g_otaLastBootPartitionLabel),
                       g_otaLastBootPartitionAddress,
                       g_otaLastBootPartitionSize);
        resetOtaTransferStats();
        g_otaStatus = Esp32BaseOta::UPLOADING;
        prepareLongOps();
        char totalBuf[32];
        formatOtaBytes(g_otaTotal, totalBuf, sizeof(totalBuf));
        ESP32BASE_LOG_I("ota", "arduino_upload_start stage=begin size=%s auth=enabled md5=arduinoota upload_target=%s address=0x%lx",
                        g_otaTotal ? totalBuf : "unknown",
                        g_otaTargetPartitionLabel[0] ? g_otaTargetPartitionLabel : "unknown",
                        static_cast<unsigned long>(g_otaTargetPartitionAddress));
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
        const esp_partition_t* bootPartition = esp_ota_get_boot_partition();
        storePartition(bootPartition,
                       g_otaLastBootPartitionLabel,
                       sizeof(g_otaLastBootPartitionLabel),
                       g_otaLastBootPartitionAddress,
                       g_otaLastBootPartitionSize);
        setOtaStage("boot_confirmed");
        ESP32BASE_LOG_I("ota", "upload_stage stage=boot_confirmed target=%s address=0x%lx boot_partition=%s boot_address=0x%lx",
                        g_otaTargetPartitionLabel[0] ? g_otaTargetPartitionLabel : "unknown",
                        static_cast<unsigned long>(g_otaTargetPartitionAddress),
                        g_otaLastBootPartitionLabel[0] ? g_otaLastBootPartitionLabel : "unknown",
                        static_cast<unsigned long>(g_otaLastBootPartitionAddress));
        if (g_otaTargetPartition && !sameOtaPartition(bootPartition, g_otaTargetPartition)) {
            g_arduinoOtaTransferActive = false;
            cleanupLongOps();
            char message[96];
            snprintf(message, sizeof(message), "boot partition mismatch target=%s boot=%s",
                     g_otaTargetPartitionLabel[0] ? g_otaTargetPartitionLabel : "unknown",
                     g_otaLastBootPartitionLabel[0] ? g_otaLastBootPartitionLabel : "unknown");
            setError(message);
            logOtaTransferSummary("arduino_upload_failed");
            g_otaStatus = Esp32BaseOta::FAILED;
            return;
        }
        g_otaStatus = Esp32BaseOta::SUCCESS;
        setOtaStage("done");
        g_arduinoOtaTransferActive = false;
        cleanupLongOps();
        char processedBuf[32];
        formatOtaBytes(g_otaProcessed, processedBuf, sizeof(processedBuf));
        ESP32BASE_LOG_I("ota", "arduino_upload_success stage=done size=%s boot_partition=%s address=0x%lx",
                        processedBuf,
                        g_otaLastBootPartitionLabel[0] ? g_otaLastBootPartitionLabel : "unknown",
                        static_cast<unsigned long>(g_otaLastBootPartitionAddress));
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
    g_otaWaitingForMarkValidLogged = false;
    g_otaMarkValidTimeoutHandled = false;
    const esp_partition_t* configuredBootPartition = esp_ota_get_boot_partition();
    const esp_partition_t* nextUpdatePartition = esp_ota_get_next_update_partition(nullptr);
    const char* runningOtaState = partitionOtaStateName(g_otaBootPartition);
    const bool waitingForMarkValid = strcmp(runningOtaState, "pending_verify") == 0;
    char runningElfSha256[65] = "";
    esp_ota_get_app_elf_sha256(runningElfSha256, sizeof(runningElfSha256));
    ESP32BASE_LOG_I("ota",
                    "boot_summary running_partition=%s address=0x%lx running_ota_state=%s configured_boot_partition=%s address=0x%lx configured_boot_state=%s next_update_partition=%s address=0x%lx next_update_state=%s mark_valid_policy=%s waiting_for_mark_valid=%s timeout_ms=%lu elf_sha256=%s",
                    partitionLabelOrUnknown(g_otaBootPartition),
                    static_cast<unsigned long>(partitionAddressOrZero(g_otaBootPartition)),
                    runningOtaState,
                    partitionLabelOrUnknown(configuredBootPartition),
                    static_cast<unsigned long>(partitionAddressOrZero(configuredBootPartition)),
                    partitionOtaStateName(configuredBootPartition),
                    partitionLabelOrUnknown(nextUpdatePartition),
                    static_cast<unsigned long>(partitionAddressOrZero(nextUpdatePartition)),
                    partitionOtaStateName(nextUpdatePartition),
#if ESP32BASE_OTA_REQUIRE_MARK_VALID
                    "require_manual",
#else
                    "arduino_default",
#endif
                    waitingForMarkValid ? "yes" : "no",
                    static_cast<unsigned long>(ESP32BASE_OTA_MARK_VALID_TIMEOUT_MS),
                    runningElfSha256[0] ? runningElfSha256 : "unavailable");
    if (g_otaBootPartition && configuredBootPartition && !sameOtaPartition(g_otaBootPartition, configuredBootPartition)) {
        ESP32BASE_LOG_W("ota", "boot_partition_mismatch running_partition=%s configured_boot_partition=%s",
                        partitionLabelOrUnknown(g_otaBootPartition),
                        partitionLabelOrUnknown(configuredBootPartition));
    }
    if (g_otaBootPartition) {
        char sizeBuf[32];
        formatOtaBytes(g_otaBootPartition->size, sizeBuf, sizeof(sizeBuf));
        ESP32BASE_LOG_I("ota", "ready running_partition=%s address=0x%lx size=%s ota_state=%s elf_sha256=%s",
                        g_otaBootPartition->label,
                        static_cast<unsigned long>(g_otaBootPartition->address),
                        sizeBuf,
                        runningOtaState,
                        runningElfSha256[0] ? runningElfSha256 : "unavailable");
    } else {
        ESP32BASE_LOG_I("ota", "ready running_partition=unknown elf_sha256=%s",
                        runningElfSha256[0] ? runningElfSha256 : "unavailable");
    }
    if (configuredBootPartition) {
        ESP32BASE_LOG_I("ota", "configured_boot_partition=%s address=0x%lx",
                        configuredBootPartition->label,
                        static_cast<unsigned long>(configuredBootPartition->address));
    }
    if (waitingForMarkValid) {
#if ESP32BASE_OTA_REQUIRE_MARK_VALID
        ESP32BASE_LOG_W("ota", "mark_valid_required app must call Esp32BaseOta::markCurrentValid() after self-check");
#else
        ESP32BASE_LOG_W("ota", "running_partition_pending_verify but ESP32BASE_OTA_REQUIRE_MARK_VALID=0; Arduino core may mark it valid before Esp32Base begins");
#endif
    }
    return true;
}

bool Esp32BaseOta::beginNetworkServices() {
    if (!g_otaReady) {
        begin();
    }
    if (g_otaNetworkServicesReady) {
        return true;
    }
    g_otaNetworkServicesReady = true;
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
    if (g_otaReady && g_otaNetworkServicesReady) {
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
    checkMarkValidTimeout();
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
    setOtaStage("begin");
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
    g_otaCalculated[0] = '\0';
    g_otaLastError[0] = '\0';
    g_otaTargetPartition = nextPartition;
    storePartition(nextPartition,
                   g_otaTargetPartitionLabel,
                   sizeof(g_otaTargetPartitionLabel),
                   g_otaTargetPartitionAddress,
                   g_otaTargetPartitionSize);
    storePartition(nullptr,
                   g_otaLastBootPartitionLabel,
                   sizeof(g_otaLastBootPartitionLabel),
                   g_otaLastBootPartitionAddress,
                   g_otaLastBootPartitionSize);
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
    ESP32BASE_LOG_I("ota", "upload_start stage=begin size=%s sha256=%s upload_target=%s address=0x%lx",
                    totalBuf,
                    g_otaExpected[0] ? "provided" : "none",
                    g_otaTargetPartitionLabel[0] ? g_otaTargetPartitionLabel : "unknown",
                    static_cast<unsigned long>(g_otaTargetPartitionAddress));
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
    Esp32BaseLongOperation::service();
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
    logUploadStage("verify");

    uint8_t digest[32];
    char actual[65];
    if (g_otaShaActive) {
        mbedtls_sha256_finish(&g_otaSha, digest);
        mbedtls_sha256_free(&g_otaSha);
        g_otaShaActive = false;
        bytesToHex(digest, sizeof(digest), actual, sizeof(actual));
        strlcpy(g_otaCalculated, actual, sizeof(g_otaCalculated));
    } else {
        actual[0] = '\0';
        g_otaCalculated[0] = '\0';
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

    logUploadStage("set_boot");
    if (!Update.end(true)) {
        cleanupLongOps();
        finishOtaTransferStats();
        setError("Update.end failed");
        logOtaTransferSummary("upload_failed");
        g_otaStatus = FAILED;
        return false;
    }

    const esp_partition_t* bootPartition = esp_ota_get_boot_partition();
    storePartition(bootPartition,
                   g_otaLastBootPartitionLabel,
                   sizeof(g_otaLastBootPartitionLabel),
                   g_otaLastBootPartitionAddress,
                   g_otaLastBootPartitionSize);
    setOtaStage("boot_confirmed");
    ESP32BASE_LOG_I("ota", "upload_stage stage=boot_confirmed target=%s address=0x%lx boot_partition=%s boot_address=0x%lx",
                    g_otaTargetPartitionLabel[0] ? g_otaTargetPartitionLabel : "unknown",
                    static_cast<unsigned long>(g_otaTargetPartitionAddress),
                    g_otaLastBootPartitionLabel[0] ? g_otaLastBootPartitionLabel : "unknown",
                    static_cast<unsigned long>(g_otaLastBootPartitionAddress));
    if (!sameOtaPartition(bootPartition, g_otaTargetPartition)) {
        setOtaStage("boot_confirmed");
        cleanupLongOps();
        finishOtaTransferStats();
        char message[96];
        snprintf(message, sizeof(message), "boot partition mismatch target=%s boot=%s",
                 g_otaTargetPartitionLabel[0] ? g_otaTargetPartitionLabel : "unknown",
                 g_otaLastBootPartitionLabel[0] ? g_otaLastBootPartitionLabel : "unknown");
        setError(message);
        logOtaTransferSummary("upload_failed");
        g_otaStatus = FAILED;
        return false;
    }

    cleanupLongOps();
    finishOtaTransferStats();
    g_otaStatus = SUCCESS;
    setOtaStage("done");
    char processedBuf[32];
    formatOtaBytes(g_otaProcessed, processedBuf, sizeof(processedBuf));
    ESP32BASE_LOG_I("ota", "upload_success stage=done size=%s sha256=%s boot_partition=%s address=0x%lx",
                    processedBuf,
                    actual[0] ? actual : "unavailable",
                    g_otaLastBootPartitionLabel[0] ? g_otaLastBootPartitionLabel : "unknown",
                    static_cast<unsigned long>(g_otaLastBootPartitionAddress));
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

void Esp32BaseOta::rejectUpload(const char* reason) {
    g_otaTotal = 0;
    g_otaProcessed = 0;
    g_otaExpected[0] = '\0';
    g_otaCalculated[0] = '\0';
    g_otaTargetPartition = nullptr;
    storePartition(nullptr,
                   g_otaTargetPartitionLabel,
                   sizeof(g_otaTargetPartitionLabel),
                   g_otaTargetPartitionAddress,
                   g_otaTargetPartitionSize);
    setError(reason ? reason : "ota upload rejected");
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

const char* Esp32BaseOta::expectedSha256() {
    return g_otaExpected;
}

const char* Esp32BaseOta::calculatedSha256() {
    return g_otaCalculated;
}

const char* Esp32BaseOta::lastTargetPartitionLabel() {
    return g_otaTargetPartitionLabel;
}

uint32_t Esp32BaseOta::lastTargetPartitionAddress() {
    return g_otaTargetPartitionAddress;
}

size_t Esp32BaseOta::lastTargetPartitionSize() {
    return g_otaTargetPartitionSize;
}

const char* Esp32BaseOta::lastBootPartitionLabel() {
    return g_otaLastBootPartitionLabel;
}

uint32_t Esp32BaseOta::lastBootPartitionAddress() {
    return g_otaLastBootPartitionAddress;
}

size_t Esp32BaseOta::lastBootPartitionSize() {
    return g_otaLastBootPartitionSize;
}

const char* Esp32BaseOta::runningOtaState() {
    return partitionOtaStateName(esp_ota_get_running_partition());
}

bool Esp32BaseOta::waitingForMarkValid() {
#if ESP32BASE_OTA_REQUIRE_MARK_VALID
    esp_ota_img_states_t state;
    esp_err_t err;
    return currentRunningOtaState(state, err) && state == ESP_OTA_IMG_PENDING_VERIFY;
#else
    return false;
#endif
}

uint32_t Esp32BaseOta::markValidElapsedMs() {
    if (!g_otaReady || g_otaBootMs == 0) {
        return 0;
    }
    return millis() - g_otaBootMs;
}

uint32_t Esp32BaseOta::markValidTimeoutMs() {
    return ESP32BASE_OTA_MARK_VALID_TIMEOUT_MS;
}

bool Esp32BaseOta::markCurrentValid() {
    const esp_partition_t* running = esp_ota_get_running_partition();
    esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
    const esp_err_t stateErr = running ? esp_ota_get_state_partition(running, &state) : ESP_ERR_NOT_FOUND;
    if (stateErr != ESP_OK) {
        ESP32BASE_LOG_W("ota",
                        "mark_valid skipped partition=%s address=0x%lx state=unknown err=%s",
                        partitionLabelOrUnknown(running),
                        static_cast<unsigned long>(partitionAddressOrZero(running)),
                        esp_err_to_name(stateErr));
        return false;
    }
    if (state != ESP_OTA_IMG_PENDING_VERIFY) {
        ESP32BASE_LOG_W("ota",
                        "mark_valid skipped partition=%s address=0x%lx state=%s reason=not_pending_verify",
                        partitionLabelOrUnknown(running),
                        static_cast<unsigned long>(partitionAddressOrZero(running)),
                        otaStateName(state));
        return false;
    }
    const esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    if (err == ESP_OK) {
        g_otaMarkValidTimeoutHandled = true;
        ESP32BASE_LOG_I("ota",
                        "mark_valid success partition=%s address=0x%lx previous_state=%s elapsed_ms=%lu",
                        partitionLabelOrUnknown(running),
                        static_cast<unsigned long>(partitionAddressOrZero(running)),
                        otaStateName(state),
                        static_cast<unsigned long>(markValidElapsedMs()));
        return true;
    }
    ESP32BASE_LOG_E("ota",
                    "mark_valid failed partition=%s address=0x%lx state=%s err=%s",
                    partitionLabelOrUnknown(running),
                    static_cast<unsigned long>(partitionAddressOrZero(running)),
                    otaStateName(state),
                    esp_err_to_name(err));
    return false;
}

bool Esp32BaseOta::isRollbackPossible() {
    return esp_ota_check_rollback_is_possible();
}

void Esp32BaseOta::rollbackAndRestart(const char* reason) {
    const char* rollbackReason = reason ? reason : "ota rollback";
    const esp_partition_t* running = esp_ota_get_running_partition();
    esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
    const esp_err_t stateErr = running ? esp_ota_get_state_partition(running, &state) : ESP_ERR_NOT_FOUND;
    Esp32BaseSystem::appendRestartLog(rollbackReason);
    Esp32BaseConfig::flushAll();
#if ESP32BASE_ENABLE_FILELOG
    Esp32BaseFileLog::flush();
#endif
    ESP32BASE_LOG_E("ota",
                    "rollback_restart reason=%s partition=%s address=0x%lx state=%s state_err=%s rollback_possible=%s",
                    rollbackReason,
                    partitionLabelOrUnknown(running),
                    static_cast<unsigned long>(partitionAddressOrZero(running)),
                    stateErr == ESP_OK ? otaStateName(state) : "unknown",
                    esp_err_to_name(stateErr),
                    esp_ota_check_rollback_is_possible() ? "yes" : "no");
#if ESP32BASE_ENABLE_FILELOG
    Esp32BaseFileLog::flush();
#endif
    delay(50);
    const esp_err_t err = esp_ota_mark_app_invalid_rollback_and_reboot();
    if (err != ESP_OK) {
        ESP32BASE_LOG_E("ota", "rollback_mark_invalid_failed err=%s fallback=restart", esp_err_to_name(err));
        Esp32BaseSystem::restart(rollbackReason);
    }
}

#endif
