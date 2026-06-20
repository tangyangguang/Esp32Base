#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

class Esp32BaseOta {
public:
    static constexpr const char* EVENT_START = "ota.start";
    static constexpr const char* EVENT_PROGRESS = "ota.progress";
    static constexpr const char* EVENT_SUCCESS = "ota.success";
    static constexpr const char* EVENT_FAILED = "ota.failed";

    enum Status : uint8_t {
        IDLE,
        READY,
        UPLOADING,
        VERIFYING,
        SUCCESS,
        FAILED
    };

    static bool begin();
    static bool beginNetworkServices();
    static void handle();
    static bool isReady();
    static bool isUploading();
    static Status status();

    static bool startUpload(size_t totalSize, const char* expectedSha256Hex = nullptr);
    static bool writeChunk(const uint8_t* data, size_t len);
    static bool finishUpload();
    static void rejectUpload(const char* reason);
    static void abortUpload(const char* reason);

    static uint8_t progress();
    static size_t bytesProcessed();
    static size_t totalSize();
    static uint32_t elapsedMs();
    static uint32_t averageBytesPerSecond();
    static const char* lastError();
    static const char* expectedSha256();
    static const char* calculatedSha256();
    static const char* lastTargetPartitionLabel();
    static uint32_t lastTargetPartitionAddress();
    static size_t lastTargetPartitionSize();
    static const char* lastBootPartitionLabel();
    static uint32_t lastBootPartitionAddress();
    static size_t lastBootPartitionSize();
    static const char* runningOtaState();
    static bool waitingForMarkValid();
    static uint32_t markValidElapsedMs();
    static uint32_t markValidTimeoutMs();

    static bool markCurrentValid();
    static bool isRollbackPossible();
    static void rollbackAndRestart(const char* reason);
};
