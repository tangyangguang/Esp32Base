#include "../Esp32BaseProfile.h"

#if ESP32BASE_ENABLE_FS

#include "Esp32BaseStorage.h"
#include "Esp32BaseFs.h"
#include "internal/Esp32BaseFsInternal.h"
#include "../core/Esp32BaseLog.h"
#if ESP32BASE_ENABLE_FILELOG
#include "Esp32BaseFileLog.h"
#endif
#if ESP32BASE_ENABLE_RECORD_STORE
#include "Esp32BaseRecordStore.h"
#endif

#include <string.h>

namespace {

Esp32BaseStorage::StorageState g_storageState = Esp32BaseStorage::StorageState::Unavailable;
Esp32BaseStorage::StorageError g_storageError = Esp32BaseStorage::StorageError::None;
Esp32BaseStorage::StorageState g_stateBeforeOta = Esp32BaseStorage::StorageState::Unavailable;
Esp32BaseStorage::StorageError g_errorBeforeOta = Esp32BaseStorage::StorageError::None;
#if ESP32BASE_ENABLE_RECORD_STORE
Esp32BaseRecordStore* g_recordStores[Esp32BaseStorage::MAX_RECORD_STORES] = {};
uint8_t g_recordStoreCount = 0;
uint32_t g_recordStoreBudgetBytes = 0;
#endif

void setStorageError(Esp32BaseStorage::StorageError error) {
    g_storageError = error;
}

size_t storageSafetyReserve(size_t totalBytes) {
    const size_t fractional = totalBytes / 4U;
    return fractional > ESP32BASE_FS_MINIMUM_SAFETY_RESERVE_BYTES
               ? fractional
               : static_cast<size_t>(ESP32BASE_FS_MINIMUM_SAFETY_RESERVE_BYTES);
}

uint32_t configuredFileLogBudget() {
#if ESP32BASE_ENABLE_FILELOG
    const uint64_t value = static_cast<uint64_t>(ESP32BASE_EB_FILELOG_MAX_BYTES) *
                           static_cast<uint64_t>(ESP32BASE_EB_FILELOG_ROTATE_FILES);
    return value > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(value);
#else
    return 0;
#endif
}

uint32_t currentFileLogBytes() {
#if ESP32BASE_ENABLE_FILELOG
    uint64_t total = 0;
    for (uint8_t i = 0; i < Esp32BaseFileLog::rotateFiles(); ++i) {
        total += Esp32BaseFileLog::segmentSize(i);
    }
    return total > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(total);
#else
    return 0;
#endif
}

uint32_t configuredRecordStoreBudget() {
#if ESP32BASE_ENABLE_RECORD_STORE
    return g_recordStoreBudgetBytes;
#else
    return 0;
#endif
}

bool pathInside(const char* path, const char* root) {
    if (!path || !root || root[0] != '/') {
        return false;
    }
    const size_t length = strlen(root);
    if (strncmp(path, root, length) != 0) {
        return false;
    }
    return path[length] == '\0' || path[length] == '/';
}

#if ESP32BASE_ENABLE_RECORD_STORE
uint64_t remainingRecordReservation() {
    uint64_t remaining = 0;
    for (uint8_t i = 0; i < g_recordStoreCount; ++i) {
        Esp32BaseRecordStore::StoreStatus status;
        if (!g_recordStores[i] || !g_recordStores[i]->readStatus(status)) {
            continue;
        }
        if (status.maximumStoreBytes > status.currentStoreBytes) {
            remaining += status.maximumStoreBytes - status.currentStoreBytes;
        }
    }
    return remaining;
}
#endif

} // namespace

bool Esp32BaseStorage::begin() {
    if (!Esp32BaseFs::isReady()) {
        g_storageState = StorageState::Unavailable;
        setStorageError(StorageError::FileSystemUnavailable);
        return false;
    }
    size_t total = 0;
    size_t used = 0;
    if (!Esp32BaseFs::storageInfo(total, used)) {
        g_storageState = StorageState::Unavailable;
        setStorageError(StorageError::FileSystemUnavailable);
        return false;
    }
    const uint64_t required = static_cast<uint64_t>(configuredRecordStoreBudget()) +
                              configuredFileLogBudget() +
                              storageSafetyReserve(total);
    if (required > total) {
        g_storageState = StorageState::Unavailable;
        setStorageError(StorageError::PartitionBudgetExceeded);
        return false;
    }
    g_storageState = StorageState::Ready;
    setStorageError(StorageError::None);
    return true;
}

#if ESP32BASE_ENABLE_RECORD_STORE
bool Esp32BaseStorage::registerRecordStore(Esp32BaseRecordStore& store) {
    Esp32BaseRecordStore::StoreStatus status;
    if (!store.readStatus(status) || !status.path || !status.path[0]) {
        setStorageError(StorageError::InvalidRecordStore);
        return false;
    }
    for (uint8_t i = 0; i < g_recordStoreCount; ++i) {
        if (g_recordStores[i] == &store) {
            return true;
        }
        Esp32BaseRecordStore::StoreStatus registered;
        if (g_recordStores[i] && g_recordStores[i]->readStatus(registered) &&
            registered.path && strcmp(registered.path, status.path) == 0) {
            setStorageError(StorageError::DuplicatePath);
            return false;
        }
    }
    if (g_recordStoreCount >= MAX_RECORD_STORES) {
        setStorageError(StorageError::TooManyRecordStores);
        return false;
    }
    const uint64_t nextRecordBudget = static_cast<uint64_t>(g_recordStoreBudgetBytes) +
                                      status.maximumStoreBytes;
    if (nextRecordBudget > ESP32BASE_RECORD_STORE_TOTAL_MAX_BYTES) {
        setStorageError(StorageError::RecordBudgetExceeded);
        return false;
    }
    size_t total = 0;
    size_t used = 0;
    if (!Esp32BaseFs::storageInfo(total, used)) {
        setStorageError(StorageError::FileSystemUnavailable);
        return false;
    }
    const uint64_t required = nextRecordBudget + configuredFileLogBudget() +
                              storageSafetyReserve(total);
    if (required > total) {
        setStorageError(StorageError::PartitionBudgetExceeded);
        return false;
    }
    g_recordStores[g_recordStoreCount++] = &store;
    g_recordStoreBudgetBytes = static_cast<uint32_t>(nextRecordBudget);
    setStorageError(StorageError::None);
    ESP32BASE_LOG_I("storage", "record_store_registered path=%s count=%u budget=%lu total_budget=%lu",
                    status.path,
                    static_cast<unsigned>(g_recordStoreCount),
                    static_cast<unsigned long>(status.maximumStoreBytes),
                    static_cast<unsigned long>(g_recordStoreBudgetBytes));
    return true;
}

uint8_t Esp32BaseStorage::recordStoreCount() {
    return g_recordStoreCount;
}

Esp32BaseRecordStore* Esp32BaseStorage::recordStoreAt(uint8_t index) {
    return index < g_recordStoreCount ? g_recordStores[index] : nullptr;
}

bool Esp32BaseStorage::clearRecordStores(ClearResult& result) {
    result = ClearResult();
    result.recordStoreCount = g_recordStoreCount;
    if (g_recordStoreCount == 0 || g_storageState != StorageState::Ready) {
        return false;
    }
    for (uint8_t i = 0; i < g_recordStoreCount; ++i) {
        Esp32BaseRecordStore::StoreStatus status;
        if (!g_recordStores[i] || !g_recordStores[i]->readStatus(status) || !status.ready) {
            return false;
        }
    }
    for (uint8_t i = 0; i < g_recordStoreCount; ++i) {
        Esp32BaseRecordStore* store = g_recordStores[i];
        Esp32BaseRecordStore::StoreStatus before;
        Esp32BaseRecordStore::StoreStatus after;
        if (!store->readStatus(before) || !store->clear() || !store->readStatus(after) ||
            !after.ready || after.recordCount != 0 || after.nextRecordId != before.nextRecordId) {
            break;
        }
        ++result.recordStoreClearedCount;
        if (after.state != Esp32BaseRecordStore::StoreState::Ready) {
            ++result.cleanupWarningCount;
        }
    }
    result.allCleared = result.recordStoreClearedCount == result.recordStoreCount;
    return result.allCleared;
}
#endif

bool Esp32BaseStorage::formatAndReload(FormatResult& result) {
    result = FormatResult();
#if ESP32BASE_ENABLE_RECORD_STORE
    result.recordStoreCount = g_recordStoreCount;
#endif
    if (g_storageState == StorageState::OtaWriteSuspended ||
        g_storageState == StorageState::Maintenance ||
        !esp32base_internal::fsBeginExclusiveMaintenance()) {
        setStorageError(StorageError::MaintenanceBusy);
        return false;
    }
    g_storageState = StorageState::Maintenance;
#if ESP32BASE_ENABLE_FILELOG
    Esp32BaseFileLog::flush();
#endif
    result.formatSuccess = Esp32BaseFs::format();
    result.mountSuccess = result.formatSuccess && Esp32BaseFs::begin();
    const bool storageReloadSuccess = result.mountSuccess && Esp32BaseStorage::begin();
#if ESP32BASE_ENABLE_FILELOG
    if (storageReloadSuccess) {
        result.fileLogReloadSuccess = Esp32BaseFileLog::begin();
    }
#else
    result.fileLogReloadSuccess = storageReloadSuccess;
#endif
#if ESP32BASE_ENABLE_RECORD_STORE
    if (storageReloadSuccess) {
        for (uint8_t i = 0; i < g_recordStoreCount; ++i) {
            Esp32BaseRecordStore::StoreStatus status;
            if (g_recordStores[i] && g_recordStores[i]->reload() &&
                g_recordStores[i]->readStatus(status) && status.ready) {
                ++result.recordStoreReloadedCount;
            }
        }
    }
#endif
    result.recordStoresReloadSuccess =
        result.recordStoreReloadedCount == result.recordStoreCount;
    esp32base_internal::fsEndExclusiveMaintenance();

    if (!result.formatSuccess) {
        g_storageState = Esp32BaseFs::isReady() ? StorageState::Ready : StorageState::Unavailable;
        setStorageError(StorageError::FormatFailed);
        return false;
    }
    if (!result.mountSuccess) {
        g_storageState = StorageState::Unavailable;
        setStorageError(StorageError::MountFailed);
        return false;
    }
    if (!storageReloadSuccess) {
        g_storageState = StorageState::Unavailable;
        return false;
    }
    g_storageState = StorageState::Ready;
    if (!result.fileLogReloadSuccess || !result.recordStoresReloadSuccess) {
        setStorageError(StorageError::ComponentReloadFailed);
        return false;
    }
    setStorageError(StorageError::None);
    return true;
}

bool Esp32BaseStorage::setOtaWriteSuspended(bool suspended) {
    if (suspended) {
        if (g_storageState == StorageState::Maintenance ||
            g_storageState == StorageState::OtaWriteSuspended) {
            setStorageError(StorageError::MaintenanceBusy);
            return false;
        }
#if ESP32BASE_ENABLE_FILELOG
        if (g_storageState == StorageState::Ready) {
            Esp32BaseFileLog::flush();
        }
#endif
        if (!esp32base_internal::fsSetWritesSuspended(true)) {
            setStorageError(StorageError::MaintenanceBusy);
            return false;
        }
        g_stateBeforeOta = g_storageState;
        g_errorBeforeOta = g_storageError;
        g_storageState = StorageState::OtaWriteSuspended;
        return true;
    }
    if (g_storageState != StorageState::OtaWriteSuspended) {
        return g_storageState == StorageState::Ready ||
               g_storageState == StorageState::Unavailable;
    }
    if (!esp32base_internal::fsSetWritesSuspended(false)) {
        setStorageError(StorageError::MaintenanceBusy);
        return false;
    }
    if (g_stateBeforeOta == StorageState::Ready && Esp32BaseFs::isReady()) {
        g_storageState = StorageState::Ready;
        setStorageError(StorageError::None);
    } else {
        g_storageState = StorageState::Unavailable;
        setStorageError(g_errorBeforeOta == StorageError::None
                            ? StorageError::FileSystemUnavailable
                            : g_errorBeforeOta);
    }
    return true;
}

bool Esp32BaseStorage::isManagedPath(const char* path) {
    if (!path || path[0] != '/') {
        return false;
    }
    if (pathInside(path, "/esp32base")) {
        return true;
    }
#if ESP32BASE_ENABLE_FILELOG
    const char* fileLogPath = Esp32BaseFileLog::path();
    if (fileLogPath && fileLogPath[0]) {
        const size_t length = strlen(fileLogPath);
        if (strncmp(path, fileLogPath, length) == 0 &&
            (path[length] == '\0' || path[length] == '.')) {
            return true;
        }
    }
#endif
#if ESP32BASE_ENABLE_RECORD_STORE
    for (uint8_t i = 0; i < g_recordStoreCount; ++i) {
        if (g_recordStores[i] && pathInside(path, g_recordStores[i]->path())) {
            return true;
        }
    }
#endif
    return false;
}

size_t Esp32BaseStorage::unmanagedWritableBytes() {
    size_t total = 0;
    size_t used = 0;
    if (!Esp32BaseFs::storageInfo(total, used) || used >= total) {
        return 0;
    }
    uint64_t required = storageSafetyReserve(total);
#if ESP32BASE_ENABLE_RECORD_STORE
    required += remainingRecordReservation();
#endif
#if ESP32BASE_ENABLE_FILELOG
    const uint32_t budget = configuredFileLogBudget();
    const uint32_t current = currentFileLogBytes();
    if (budget > current) {
        required += budget - current;
    }
#endif
    const uint64_t freeBytes = total - used;
    return freeBytes > required ? static_cast<size_t>(freeBytes - required) : 0;
}

bool Esp32BaseStorage::readStatus(StorageStatus& status) {
    status = StorageStatus();
    status.state = g_storageState;
    status.error = g_storageError;
    status.ready = g_storageState == StorageState::Ready;
    status.writesSuspended = esp32base_internal::fsWritesSuspended();
    Esp32BaseFs::storageInfo(status.fileSystemTotalBytes, status.fileSystemUsedBytes);
    status.fileSystemFreeBytes = status.fileSystemTotalBytes > status.fileSystemUsedBytes
                                     ? status.fileSystemTotalBytes - status.fileSystemUsedBytes
                                     : 0;
    status.minimumSafetyReserveBytes = storageSafetyReserve(status.fileSystemTotalBytes);
    status.unmanagedWritableBytes = unmanagedWritableBytes();
#if ESP32BASE_ENABLE_RECORD_STORE
    status.registeredRecordBudgetBytes = g_recordStoreBudgetBytes;
    status.recordStoreCount = g_recordStoreCount;
#endif
    status.maximumRecordBudgetBytes = ESP32BASE_RECORD_STORE_TOTAL_MAX_BYTES;
    status.fileLogBudgetBytes = configuredFileLogBudget();
    status.errorReason = storageErrorName(status.error);
    return Esp32BaseFs::isReady();
}

Esp32BaseStorage::StorageState Esp32BaseStorage::state() { return g_storageState; }
Esp32BaseStorage::StorageError Esp32BaseStorage::lastError() { return g_storageError; }
const char* Esp32BaseStorage::lastErrorReason() { return storageErrorName(g_storageError); }

const char* Esp32BaseStorage::storageStateName(StorageState value) {
    switch (value) {
        case StorageState::Unavailable: return "unavailable";
        case StorageState::Ready: return "ready";
        case StorageState::OtaWriteSuspended: return "ota_write_suspended";
        case StorageState::Maintenance: return "maintenance";
        default: return "unknown";
    }
}

const char* Esp32BaseStorage::storageErrorName(StorageError value) {
    switch (value) {
        case StorageError::None: return "none";
        case StorageError::FileSystemUnavailable: return "filesystem_unavailable";
        case StorageError::InvalidRecordStore: return "invalid_record_store";
        case StorageError::DuplicateRecordStore: return "duplicate_record_store";
        case StorageError::DuplicatePath: return "duplicate_path";
        case StorageError::TooManyRecordStores: return "too_many_record_stores";
        case StorageError::RecordBudgetExceeded: return "record_budget_exceeded";
        case StorageError::PartitionBudgetExceeded: return "partition_budget_exceeded";
        case StorageError::MaintenanceBusy: return "maintenance_busy";
        case StorageError::FormatFailed: return "format_failed";
        case StorageError::MountFailed: return "mount_failed";
        case StorageError::ComponentReloadFailed: return "component_reload_failed";
        default: return "unknown";
    }
}

#endif
