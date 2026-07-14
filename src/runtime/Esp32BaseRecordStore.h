#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

class Esp32BaseRecordStore {
public:
    static constexpr size_t MAX_RECORD_TYPE_NAME_LENGTH = 32;
    static constexpr size_t MAX_STORE_PATH_LENGTH = 96;

    enum class StoreState : uint8_t {
        Uninitialized,
        Ready,
        Degraded,
        WriteFault,
        StructuralFault
    };

    enum class StoreError : uint8_t {
        None,
        FileSystemUnavailable,
        InvalidDefinition,
        InsufficientFreeSpace,
        DirectoryCreateFailed,
        FileCreateFailed,
        FileRenameFailed,
        FileSizeMismatch,
        HeaderInvalid,
        DefinitionMismatch,
        ReadFailed,
        WriteFailed,
        VerifyFailed,
        InvalidStartTime,
        InvalidPayload,
        IdExhausted
    };

    enum class RecordReadResult : uint8_t {
        Found,
        NotFound,
        Corrupt,
        IoError,
        InvalidArgument
    };

    struct StoreDefinition {
        const char* recordTypeName = nullptr;
        uint16_t storeVersion = 1;
        uint32_t payloadSizeBytes = 0;
        uint32_t maximumFileBytes = 0;
        uint32_t minimumFileSystemFreeBytes = 0;
    };

    struct RecordStartTime {
        uint32_t bootId = 0;
        uint32_t uptimeSec = 0;
    };

    struct RecordTiming {
        uint32_t completedEpochSec = 0;
        uint32_t completedBootId = 0;
        uint32_t completedUptimeSec = 0;
        uint32_t durationSec = 0;
    };

    struct RecordView {
        uint32_t recordId = 0;
        RecordTiming timing;
        const uint8_t* payload = nullptr;
        uint32_t payloadSizeBytes = 0;
    };

    struct RecordMetadata {
        uint32_t recordId = 0;
        RecordTiming timing;
    };

    struct StoreStatus {
        StoreState state = StoreState::Uninitialized;
        StoreError error = StoreError::None;
        bool ready = false;
        bool writable = false;
        uint32_t recordCount = 0;
        uint32_t capacity = 0;
        uint32_t damagedRecordCount = 0;
        uint32_t oldestRecordId = 0;
        uint32_t newestRecordId = 0;
        uint32_t nextRecordId = 1;
        uint32_t slotSizeBytes = 0;
        uint32_t actualFileBytes = 0;
        uint32_t maximumFileBytes = 0;
        size_t fileSystemTotalBytes = 0;
        size_t fileSystemUsedBytes = 0;
        size_t fileSystemFreeBytes = 0;
        const char* path = nullptr;
        const char* errorReason = nullptr;
    };

    using ReadCallback = void (*)(const RecordView& record, void* user);

    Esp32BaseRecordStore();

    bool begin(const StoreDefinition& definition);
    bool reload();
    bool captureStartTime(RecordStartTime& startTime) const;
    bool appendInstant(const uint8_t* payload, size_t payloadSizeBytes);
    bool appendCompleted(const RecordStartTime& startTime,
                         const uint8_t* payload,
                         size_t payloadSizeBytes);
    bool readLatest(uint32_t offset,
                    uint32_t limit,
                    uint8_t* scratchBuffer,
                    size_t scratchBufferBytes,
                    ReadCallback callback,
                    void* user = nullptr);
    RecordReadResult readById(uint32_t recordId,
                              uint8_t* payloadOut,
                              size_t payloadOutBytes,
                              RecordMetadata& recordOut);
    bool clear();
    bool readStatus(StoreStatus& status) const;

    bool isReady() const;
    bool isWritable() const;
    StoreState state() const;
    StoreError lastError() const;
    const char* lastErrorReason() const;
    const char* path() const;

    static bool resolveCompletedEpoch(const RecordTiming& timing, uint32_t& epochSec);
    static bool resolveStartedEpoch(const RecordTiming& timing, uint32_t& epochSec);
    static const char* storeStateName(StoreState state);
    static const char* storeErrorName(StoreError error);

private:
    bool loadOrCreate();
    bool loadExisting();
    bool createNew();
    bool scanRecords();
    bool appendWithDuration(uint32_t durationSec, const uint8_t* payload, size_t payloadSizeBytes);
    bool writeHeader(uint8_t copyIndex, uint32_t firstVisibleId, uint32_t sequence);
    void setError(StoreError error, StoreState state);
    void resetRuntime();

    char recordTypeName_[MAX_RECORD_TYPE_NAME_LENGTH + 1];
    char path_[MAX_STORE_PATH_LENGTH];
    char tempPath_[MAX_STORE_PATH_LENGTH];
    uint16_t storeVersion_;
    uint32_t payloadSizeBytes_;
    uint32_t maximumFileBytes_;
    uint32_t minimumFileSystemFreeBytes_;
    uint32_t slotSizeBytes_;
    uint32_t capacity_;
    uint32_t actualFileBytes_;
    uint32_t firstVisibleId_;
    uint32_t headerSequence_;
    uint8_t activeHeader_;
    uint32_t recordCount_;
    uint32_t damagedRecordCount_;
    uint32_t oldestRecordId_;
    uint32_t newestRecordId_;
    uint32_t nextRecordId_;
    StoreState state_;
    StoreError error_;
    bool definitionSet_;
};
