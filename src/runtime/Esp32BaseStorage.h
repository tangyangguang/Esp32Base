#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

#include "../Esp32BaseProfile.h"

class Esp32BaseRecordStore;

class Esp32BaseStorage {
public:
    static constexpr uint8_t MAX_RECORD_STORES = 8;

    enum class StorageState : uint8_t {
        Unavailable,
        Ready,
        OtaWriteSuspended,
        Maintenance
    };

    enum class StorageError : uint8_t {
        None,
        FileSystemUnavailable,
        InvalidRecordStore,
        DuplicateRecordStore,
        DuplicatePath,
        TooManyRecordStores,
        RecordBudgetExceeded,
        PartitionBudgetExceeded,
        MaintenanceBusy,
        FormatFailed,
        MountFailed,
        ComponentReloadFailed
    };

    struct StorageStatus {
        StorageState state = StorageState::Unavailable;
        StorageError error = StorageError::None;
        bool ready = false;
        bool writesSuspended = false;
        size_t fileSystemTotalBytes = 0;
        size_t fileSystemUsedBytes = 0;
        size_t fileSystemFreeBytes = 0;
        size_t minimumSafetyReserveBytes = 0;
        size_t unmanagedWritableBytes = 0;
        uint32_t registeredRecordBudgetBytes = 0;
        uint32_t maximumRecordBudgetBytes = 0;
        uint32_t fileLogBudgetBytes = 0;
        uint8_t recordStoreCount = 0;
        const char* errorReason = nullptr;
    };

    struct FormatResult {
        bool formatSuccess = false;
        bool mountSuccess = false;
        bool fileLogReloadSuccess = false;
        uint8_t recordStoreCount = 0;
        uint8_t recordStoreReloadedCount = 0;
        bool recordStoresReloadSuccess = false;
    };

    struct ClearResult {
        uint8_t recordStoreCount = 0;
        uint8_t recordStoreClearedCount = 0;
        uint8_t cleanupWarningCount = 0;
        bool allCleared = false;
    };

    static bool begin();
#if ESP32BASE_ENABLE_RECORD_STORE
    static bool registerRecordStore(Esp32BaseRecordStore& store);
    static uint8_t recordStoreCount();
    static Esp32BaseRecordStore* recordStoreAt(uint8_t index);
    static bool clearRecordStores(ClearResult& result);
#endif
    static bool formatAndReload(FormatResult& result);
    static bool setOtaWriteSuspended(bool suspended);
    static bool isManagedPath(const char* path);
    static size_t unmanagedWritableBytes();
    static bool readStatus(StorageStatus& status);
    static StorageState state();
    static StorageError lastError();
    static const char* lastErrorReason();
    static const char* storageStateName(StorageState state);
    static const char* storageErrorName(StorageError error);
};
