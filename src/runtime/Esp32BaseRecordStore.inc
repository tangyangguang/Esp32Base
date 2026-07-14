#include "../Esp32BaseProfile.h"

#if ESP32BASE_ENABLE_RECORD_STORE

#include "Esp32BaseRecordStore.h"
#include "Esp32BaseFs.h"
#include "Esp32BaseTime.h"
#include "../core/Esp32BaseLog.h"
#include "internal/Esp32BaseFsInternal.h"
#include "internal/Esp32BaseLongOperation.h"

#include <stdio.h>
#include <string.h>

namespace {

constexpr const char* kRecordStoreDirectory = "/esp32base/records";
constexpr uint32_t kRecordStoreHeaderMagic = 0x52534844UL; // RSHD
constexpr uint16_t kRecordStoreContainerVersion = 1;
constexpr uint32_t kRecordStoreHeaderBytes = 64;
constexpr uint32_t kRecordStoreHeaderCopies = 2;
constexpr uint32_t kRecordStoreDataOffset = kRecordStoreHeaderBytes * kRecordStoreHeaderCopies;
constexpr uint32_t kRecordStoreMetadataBytes = 20;
constexpr uint32_t kRecordStoreCrcBytes = 4;

enum RecordSlotState : uint8_t {
    RECORD_SLOT_EMPTY,
    RECORD_SLOT_VALID,
    RECORD_SLOT_CORRUPT,
    RECORD_SLOT_IO_ERROR
};

struct RecordSlotData {
    uint32_t recordId;
    Esp32BaseRecordStore::RecordTiming timing;
    uint32_t storedCrc;
    uint32_t calculatedCrc;
    bool anyNonZero;
};

uint16_t recordStoreReadU16(const uint8_t* data) {
    return static_cast<uint16_t>(data[0]) |
           static_cast<uint16_t>(static_cast<uint16_t>(data[1]) << 8);
}

uint32_t recordStoreReadU32(const uint8_t* data) {
    return static_cast<uint32_t>(data[0]) |
           (static_cast<uint32_t>(data[1]) << 8) |
           (static_cast<uint32_t>(data[2]) << 16) |
           (static_cast<uint32_t>(data[3]) << 24);
}

void recordStoreWriteU16(uint8_t* data, uint16_t value) {
    data[0] = static_cast<uint8_t>(value & 0xFFU);
    data[1] = static_cast<uint8_t>((value >> 8) & 0xFFU);
}

void recordStoreWriteU32(uint8_t* data, uint32_t value) {
    data[0] = static_cast<uint8_t>(value & 0xFFUL);
    data[1] = static_cast<uint8_t>((value >> 8) & 0xFFUL);
    data[2] = static_cast<uint8_t>((value >> 16) & 0xFFUL);
    data[3] = static_cast<uint8_t>((value >> 24) & 0xFFUL);
}

uint32_t recordStoreCrcUpdate(uint32_t crc, const uint8_t* data, size_t length) {
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xEDB88320UL & (0UL - (crc & 1UL)));
        }
    }
    return crc;
}

uint32_t recordStoreCrc(const uint8_t* data, size_t length) {
    return ~recordStoreCrcUpdate(0xFFFFFFFFUL, data, length);
}

bool recordStoreTypeNameValid(const char* name) {
    if (!name || !name[0]) {
        return false;
    }
    const size_t length = strlen(name);
    if (length == 0 || length > Esp32BaseRecordStore::MAX_RECORD_TYPE_NAME_LENGTH ||
        name[0] == '-' || name[length - 1] == '-') {
        return false;
    }
    for (const char* p = name; *p; ++p) {
        const char c = *p;
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-') {
            continue;
        }
        return false;
    }
    return true;
}

uint32_t recordStoreSlotOffset(uint32_t slotSize, uint32_t slot) {
    return kRecordStoreDataOffset + slotSize * slot;
}

struct RecordVisitContext {
    uint32_t position;
    uint32_t slotSize;
    uint32_t payloadSize;
    uint32_t crc;
    uint8_t metadata[kRecordStoreMetadataBytes];
    uint8_t crcBytes[kRecordStoreCrcBytes];
    uint8_t* payloadOut;
    bool anyNonZero;
};

bool recordStoreVisit(const uint8_t* data, size_t length, void* user) {
    RecordVisitContext* context = static_cast<RecordVisitContext*>(user);
    if (!context || (!data && length > 0)) {
        return false;
    }
    for (size_t i = 0; i < length; ++i, ++context->position) {
        const uint8_t value = data[i];
        context->anyNonZero = context->anyNonZero || value != 0;
        if (context->position < context->slotSize - kRecordStoreCrcBytes) {
            context->crc = recordStoreCrcUpdate(context->crc, &value, 1);
        }
        if (context->position < kRecordStoreMetadataBytes) {
            context->metadata[context->position] = value;
        } else if (context->position < kRecordStoreMetadataBytes + context->payloadSize) {
            if (context->payloadOut) {
                context->payloadOut[context->position - kRecordStoreMetadataBytes] = value;
            }
        } else if (context->position < context->slotSize) {
            context->crcBytes[context->position - (context->slotSize - kRecordStoreCrcBytes)] = value;
        }
    }
    return true;
}

RecordSlotState recordStoreFinishVisit(const RecordVisitContext& context,
                                       uint32_t capacity,
                                       uint32_t slot,
                                       RecordSlotData& out) {
    memset(&out, 0, sizeof(out));
    out.recordId = recordStoreReadU32(context.metadata);
    out.timing.completedEpochSec = recordStoreReadU32(context.metadata + 4);
    out.timing.completedBootId = recordStoreReadU32(context.metadata + 8);
    out.timing.completedUptimeSec = recordStoreReadU32(context.metadata + 12);
    out.timing.durationSec = recordStoreReadU32(context.metadata + 16);
    out.storedCrc = recordStoreReadU32(context.crcBytes);
    out.calculatedCrc = ~context.crc;
    out.anyNonZero = context.anyNonZero;
    if (!out.anyNonZero) {
        return RECORD_SLOT_EMPTY;
    }
    if (out.recordId == 0 || out.timing.completedBootId == 0 ||
        out.timing.durationSec > out.timing.completedUptimeSec ||
        out.storedCrc != out.calculatedCrc ||
        ((out.recordId - 1U) % capacity) != slot) {
        return RECORD_SLOT_CORRUPT;
    }
    return RECORD_SLOT_VALID;
}

RecordSlotState recordStoreReadSlot(const char* path,
                                    uint32_t slotSize,
                                    uint32_t payloadSize,
                                    uint32_t capacity,
                                    uint32_t slot,
                                    uint8_t* payloadOut,
                                    RecordSlotData& out) {
    memset(&out, 0, sizeof(out));
    if (!path || slot >= capacity) {
        return RECORD_SLOT_IO_ERROR;
    }
    RecordVisitContext context = {};
    context.slotSize = slotSize;
    context.payloadSize = payloadSize;
    context.crc = 0xFFFFFFFFUL;
    context.payloadOut = payloadOut;
    if (!esp32base_internal::fsVisitBytesAt(path,
                                            recordStoreSlotOffset(slotSize, slot),
                                            slotSize,
                                            recordStoreVisit,
                                            &context)) {
        return RECORD_SLOT_IO_ERROR;
    }
    return recordStoreFinishVisit(context, capacity, slot, out);
}

using RecordSlotVisitor = bool (*)(uint32_t slot,
                                   RecordSlotState state,
                                   const RecordSlotData& record,
                                   void* user);

struct RecordSlotsVisitContext {
    uint32_t slotSize;
    uint32_t payloadSize;
    uint32_t capacity;
    uint8_t* payloadOut;
    RecordVisitContext recordVisit;
    RecordSlotVisitor visitor;
    void* user;
};

bool recordStoreVisitFixedSlot(uint32_t slot,
                               uint32_t slotByteOffset,
                               const uint8_t* data,
                               size_t length,
                               void* user) {
    RecordSlotsVisitContext* context = static_cast<RecordSlotsVisitContext*>(user);
    if (!context || !context->visitor || slotByteOffset + length > context->slotSize) {
        return false;
    }
    if (slotByteOffset == 0) {
        memset(&context->recordVisit, 0, sizeof(context->recordVisit));
        context->recordVisit.slotSize = context->slotSize;
        context->recordVisit.payloadSize = context->payloadSize;
        context->recordVisit.crc = 0xFFFFFFFFUL;
        context->recordVisit.payloadOut = context->payloadOut;
    }
    if (context->recordVisit.position != slotByteOffset ||
        !recordStoreVisit(data, length, &context->recordVisit)) {
        return false;
    }
    if (context->recordVisit.position == context->slotSize) {
        RecordSlotData record = {};
        const RecordSlotState state = recordStoreFinishVisit(context->recordVisit,
                                                              context->capacity,
                                                              slot,
                                                              record);
        return context->visitor(slot, state, record, context->user);
    }
    return true;
}

bool recordStoreVisitSlots(const char* path,
                           uint32_t slotSize,
                           uint32_t payloadSize,
                           uint32_t capacity,
                           uint32_t firstSlot,
                           uint32_t slotCount,
                           bool descending,
                           uint8_t* payloadOut,
                           RecordSlotVisitor visitor,
                           void* user) {
    RecordSlotsVisitContext context = {};
    context.slotSize = slotSize;
    context.payloadSize = payloadSize;
    context.capacity = capacity;
    context.payloadOut = payloadOut;
    context.visitor = visitor;
    context.user = user;
    return esp32base_internal::fsVisitFixedSlots(path,
                                                 kRecordStoreDataOffset,
                                                 slotSize,
                                                 capacity,
                                                 firstSlot,
                                                 slotCount,
                                                 descending,
                                                 recordStoreVisitFixedSlot,
                                                 &context);
}

void recordStoreBuildHeader(uint8_t* header,
                            uint16_t storeVersion,
                            uint32_t payloadSize,
                            uint32_t slotSize,
                            uint32_t capacity,
                            uint32_t fileSize,
                            uint32_t firstVisibleId,
                            uint32_t sequence) {
    memset(header, 0, kRecordStoreHeaderBytes);
    recordStoreWriteU32(header, kRecordStoreHeaderMagic);
    recordStoreWriteU16(header + 4, kRecordStoreContainerVersion);
    recordStoreWriteU16(header + 6, static_cast<uint16_t>(kRecordStoreHeaderBytes));
    recordStoreWriteU16(header + 8, storeVersion);
    recordStoreWriteU32(header + 12, payloadSize);
    recordStoreWriteU32(header + 16, slotSize);
    recordStoreWriteU32(header + 20, capacity);
    recordStoreWriteU32(header + 24, fileSize);
    recordStoreWriteU32(header + 28, firstVisibleId ? firstVisibleId : 1);
    recordStoreWriteU32(header + 32, sequence);
    recordStoreWriteU32(header + 36, 0);
    recordStoreWriteU32(header + 36, recordStoreCrc(header, kRecordStoreHeaderBytes));
}

bool recordStoreHeaderCrcValid(const uint8_t* header) {
    uint8_t copy[kRecordStoreHeaderBytes];
    memcpy(copy, header, sizeof(copy));
    const uint32_t stored = recordStoreReadU32(copy + 36);
    recordStoreWriteU32(copy + 36, 0);
    return stored == recordStoreCrc(copy, sizeof(copy));
}

bool recordStoreReadHeader(const char* path, uint8_t copyIndex, uint8_t* header) {
    size_t readLength = 0;
    return Esp32BaseFs::readBytesAt(path,
                                    static_cast<uint32_t>(copyIndex) * kRecordStoreHeaderBytes,
                                    header,
                                    kRecordStoreHeaderBytes,
                                    &readLength) &&
           readLength == kRecordStoreHeaderBytes;
}

bool recordStoreHeaderValid(const uint8_t* header) {
    return recordStoreReadU32(header) == kRecordStoreHeaderMagic &&
           recordStoreReadU16(header + 4) == kRecordStoreContainerVersion &&
           recordStoreReadU16(header + 6) == kRecordStoreHeaderBytes &&
           recordStoreReadU16(header + 10) == 0 &&
           recordStoreReadU32(header + 28) != 0 &&
           recordStoreHeaderCrcValid(header);
}

bool recordStoreHeaderMatches(const uint8_t* header,
                              uint16_t storeVersion,
                              uint32_t payloadSize,
                              uint32_t slotSize,
                              uint32_t capacity,
                              uint32_t fileSize) {
    return recordStoreHeaderValid(header) &&
           recordStoreReadU16(header + 8) == storeVersion &&
           recordStoreReadU32(header + 12) == payloadSize &&
           recordStoreReadU32(header + 16) == slotSize &&
           recordStoreReadU32(header + 20) == capacity &&
           recordStoreReadU32(header + 24) == fileSize;
}

bool recordStoreSequenceNewerOrEqual(uint32_t left, uint32_t right) {
    return static_cast<int32_t>(left - right) >= 0;
}

struct RecordMaximumScanContext {
    uint32_t firstVisibleId;
    uint32_t capacity;
    uint32_t maximumValidId;
    uint32_t minimumCorruptId;
};

bool recordStoreFindMaximum(uint32_t slot,
                            RecordSlotState state,
                            const RecordSlotData& record,
                            void* user) {
    RecordMaximumScanContext* context = static_cast<RecordMaximumScanContext*>(user);
    if (!context) return false;
    const bool idPositionValid = record.recordId != 0 &&
                                 ((record.recordId - 1U) % context->capacity) == slot;
    if (state == RECORD_SLOT_VALID && record.recordId >= context->firstVisibleId &&
        record.recordId > context->maximumValidId) {
        context->maximumValidId = record.recordId;
    } else if (state == RECORD_SLOT_CORRUPT && idPositionValid &&
               record.recordId >= context->firstVisibleId &&
               (context->minimumCorruptId == 0 || record.recordId < context->minimumCorruptId)) {
        context->minimumCorruptId = record.recordId;
    }
    return true;
}

struct RecordStatusScanContext {
    uint32_t capacity;
    uint32_t firstVisibleId;
    uint32_t visibleFloor;
    uint32_t maximumId;
    uint32_t recordCount;
    uint32_t damagedRecordCount;
    uint32_t oldestRecordId;
};

bool recordStoreCollectStatus(uint32_t slot,
                              RecordSlotState state,
                              const RecordSlotData& record,
                              void* user) {
    RecordStatusScanContext* context = static_cast<RecordStatusScanContext*>(user);
    if (!context) return false;
    uint32_t expectedId = 0;
    if (context->maximumId != 0) {
        const uint32_t newestSlot = (context->maximumId - 1U) % context->capacity;
        const uint32_t distance = newestSlot >= slot
            ? newestSlot - slot
            : context->capacity - (slot - newestSlot);
        if (context->maximumId > distance) {
            const uint32_t candidate = context->maximumId - distance;
            if (candidate >= context->visibleFloor) expectedId = candidate;
        }
    }
    if (expectedId != 0 && state == RECORD_SLOT_VALID && record.recordId == expectedId) {
        ++context->recordCount;
        if (context->oldestRecordId == 0 || expectedId < context->oldestRecordId) {
            context->oldestRecordId = expectedId;
        }
    } else if (expectedId != 0 ||
               (state == RECORD_SLOT_CORRUPT && record.recordId >= context->firstVisibleId &&
                record.recordId > context->maximumId)) {
        ++context->damagedRecordCount;
    }
    return true;
}

struct RecordLatestReadContext {
    uint32_t expectedId;
    uint32_t offset;
    uint32_t limit;
    uint32_t skipped;
    uint32_t emitted;
    uint32_t payloadSize;
    uint8_t* payload;
    Esp32BaseRecordStore::ReadCallback callback;
    void* user;
    bool completed;
};

bool recordStoreReadLatestSlot(uint32_t,
                               RecordSlotState state,
                               const RecordSlotData& record,
                               void* user) {
    RecordLatestReadContext* context = static_cast<RecordLatestReadContext*>(user);
    if (!context || context->expectedId == 0) return false;
    if (state == RECORD_SLOT_VALID && record.recordId == context->expectedId) {
        if (context->skipped < context->offset) {
            ++context->skipped;
        } else {
            Esp32BaseRecordStore::RecordView view;
            view.recordId = record.recordId;
            view.timing = record.timing;
            view.payload = context->payload;
            view.payloadSizeBytes = context->payloadSize;
            context->callback(view, context->user);
            ++context->emitted;
            if (context->emitted >= context->limit) {
                context->completed = true;
                return false;
            }
        }
    }
    --context->expectedId;
    return true;
}

} // namespace

Esp32BaseRecordStore::Esp32BaseRecordStore()
    : storeVersion_(0), payloadSizeBytes_(0), maximumFileBytes_(0),
      minimumFileSystemFreeBytes_(0), slotSizeBytes_(0), capacity_(0),
      actualFileBytes_(0), firstVisibleId_(1), headerSequence_(0), activeHeader_(0),
      recordCount_(0), damagedRecordCount_(0), oldestRecordId_(0), newestRecordId_(0),
      nextRecordId_(1), state_(StoreState::Uninitialized), error_(StoreError::None),
      definitionSet_(false) {
    recordTypeName_[0] = '\0';
    path_[0] = '\0';
    tempPath_[0] = '\0';
}

void Esp32BaseRecordStore::resetRuntime() {
    firstVisibleId_ = 1;
    headerSequence_ = 0;
    activeHeader_ = 0;
    recordCount_ = 0;
    damagedRecordCount_ = 0;
    oldestRecordId_ = 0;
    newestRecordId_ = 0;
    nextRecordId_ = 1;
    state_ = StoreState::Uninitialized;
    error_ = StoreError::None;
}

void Esp32BaseRecordStore::setError(StoreError error, StoreState state) {
    error_ = error;
    state_ = state;
    ESP32BASE_LOG_W("record_store", "fault path=%s state=%s error=%s",
                    path_[0] ? path_ : "-", storeStateName(state_), storeErrorName(error_));
}

bool Esp32BaseRecordStore::begin(const StoreDefinition& definition) {
    resetRuntime();
    definitionSet_ = false;
    if (!recordStoreTypeNameValid(definition.recordTypeName) || definition.storeVersion == 0 ||
        definition.payloadSizeBytes == 0 || definition.payloadSizeBytes > UINT32_MAX - 24U ||
        definition.maximumFileBytes < kRecordStoreDataOffset + definition.payloadSizeBytes + 24U) {
        setError(StoreError::InvalidDefinition, StoreState::StructuralFault);
        return false;
    }
    const uint32_t slotSize = definition.payloadSizeBytes + 24U;
    const uint32_t capacity = (definition.maximumFileBytes - kRecordStoreDataOffset) / slotSize;
    if (capacity == 0 || capacity > UINT32_MAX / slotSize) {
        setError(StoreError::InvalidDefinition, StoreState::StructuralFault);
        return false;
    }
    const uint32_t actualSize = kRecordStoreDataOffset + capacity * slotSize;
    const int pathLength = snprintf(path_, sizeof(path_), "%s/%s.v%u.bin",
                                    kRecordStoreDirectory,
                                    definition.recordTypeName,
                                    static_cast<unsigned>(definition.storeVersion));
    const int tempLength = snprintf(tempPath_, sizeof(tempPath_), "%s.tmp", path_);
    if (pathLength <= 0 || static_cast<size_t>(pathLength) >= sizeof(path_) ||
        tempLength <= 0 || static_cast<size_t>(tempLength) >= sizeof(tempPath_)) {
        path_[0] = '\0';
        tempPath_[0] = '\0';
        setError(StoreError::InvalidDefinition, StoreState::StructuralFault);
        return false;
    }
    strlcpy(recordTypeName_, definition.recordTypeName, sizeof(recordTypeName_));
    storeVersion_ = definition.storeVersion;
    payloadSizeBytes_ = definition.payloadSizeBytes;
    maximumFileBytes_ = definition.maximumFileBytes;
    minimumFileSystemFreeBytes_ = definition.minimumFileSystemFreeBytes;
    slotSizeBytes_ = slotSize;
    capacity_ = capacity;
    actualFileBytes_ = actualSize;
    definitionSet_ = true;
    return loadOrCreate();
}

bool Esp32BaseRecordStore::reload() {
    if (!definitionSet_) {
        setError(StoreError::InvalidDefinition, StoreState::StructuralFault);
        return false;
    }
    resetRuntime();
    return loadOrCreate();
}

bool Esp32BaseRecordStore::loadOrCreate() {
    if (!Esp32BaseFs::isReady()) {
        setError(StoreError::FileSystemUnavailable, StoreState::StructuralFault);
        return false;
    }
    if (Esp32BaseFs::exists(path_)) {
        return loadExisting();
    }
    return createNew();
}

bool Esp32BaseRecordStore::createNew() {
    const size_t freeBytes = Esp32BaseFs::freeBytes();
    const uint64_t required = static_cast<uint64_t>(actualFileBytes_) + minimumFileSystemFreeBytes_;
    if (freeBytes < required) {
        setError(StoreError::InsufficientFreeSpace, StoreState::StructuralFault);
        return false;
    }
    if ((!Esp32BaseFs::exists("/esp32base") && !Esp32BaseFs::mkdir("/esp32base")) ||
        (!Esp32BaseFs::exists(kRecordStoreDirectory) && !Esp32BaseFs::mkdir(kRecordStoreDirectory))) {
        setError(StoreError::DirectoryCreateFailed, StoreState::StructuralFault);
        return false;
    }
    if (Esp32BaseFs::exists(tempPath_)) {
        Esp32BaseFs::removeFileWithRecovery(tempPath_);
    }
    if (!Esp32BaseFs::createFixedFile(tempPath_, actualFileBytes_, 0)) {
        setError(StoreError::FileCreateFailed, StoreState::StructuralFault);
        return false;
    }

    uint8_t headerA[kRecordStoreHeaderBytes];
    uint8_t headerB[kRecordStoreHeaderBytes];
    recordStoreBuildHeader(headerA, storeVersion_, payloadSizeBytes_, slotSizeBytes_, capacity_, actualFileBytes_, 1, 1);
    recordStoreBuildHeader(headerB, storeVersion_, payloadSizeBytes_, slotSizeBytes_, capacity_, actualFileBytes_, 1, 0);
    if (!Esp32BaseFs::writeBytesAt(tempPath_, 0, headerA, sizeof(headerA)) ||
        !Esp32BaseFs::writeBytesAt(tempPath_, kRecordStoreHeaderBytes, headerB, sizeof(headerB))) {
        Esp32BaseFs::removeFileWithRecovery(tempPath_);
        setError(StoreError::WriteFailed, StoreState::StructuralFault);
        return false;
    }
    uint8_t verifyA[kRecordStoreHeaderBytes];
    uint8_t verifyB[kRecordStoreHeaderBytes];
    if (!recordStoreReadHeader(tempPath_, 0, verifyA) || !recordStoreReadHeader(tempPath_, 1, verifyB) ||
        !recordStoreHeaderMatches(verifyA, storeVersion_, payloadSizeBytes_, slotSizeBytes_, capacity_, actualFileBytes_) ||
        !recordStoreHeaderMatches(verifyB, storeVersion_, payloadSizeBytes_, slotSizeBytes_, capacity_, actualFileBytes_)) {
        Esp32BaseFs::removeFileWithRecovery(tempPath_);
        setError(StoreError::VerifyFailed, StoreState::StructuralFault);
        return false;
    }
    if (!Esp32BaseFs::rename(tempPath_, path_)) {
        Esp32BaseFs::removeFileWithRecovery(tempPath_);
        setError(StoreError::FileRenameFailed, StoreState::StructuralFault);
        return false;
    }
    ESP32BASE_LOG_I("record_store",
                    "created path=%s max_bytes=%lu actual_bytes=%lu slot_bytes=%lu capacity=%lu",
                    path_, static_cast<unsigned long>(maximumFileBytes_),
                    static_cast<unsigned long>(actualFileBytes_),
                    static_cast<unsigned long>(slotSizeBytes_),
                    static_cast<unsigned long>(capacity_));
    return loadExisting();
}

bool Esp32BaseRecordStore::loadExisting() {
    const int64_t fileSize = Esp32BaseFs::fileSize(path_);
    if (fileSize != static_cast<int64_t>(actualFileBytes_)) {
        setError(StoreError::FileSizeMismatch, StoreState::StructuralFault);
        return false;
    }
    uint8_t headerA[kRecordStoreHeaderBytes];
    uint8_t headerB[kRecordStoreHeaderBytes];
    const bool readA = recordStoreReadHeader(path_, 0, headerA);
    const bool readB = recordStoreReadHeader(path_, 1, headerB);
    const bool headerValidA = readA && recordStoreHeaderValid(headerA);
    const bool headerValidB = readB && recordStoreHeaderValid(headerB);
    if (!headerValidA && !headerValidB) {
        setError(StoreError::HeaderInvalid, StoreState::StructuralFault);
        return false;
    }
    const bool validA = headerValidA && recordStoreHeaderMatches(headerA, storeVersion_, payloadSizeBytes_, slotSizeBytes_, capacity_, actualFileBytes_);
    const bool validB = headerValidB && recordStoreHeaderMatches(headerB, storeVersion_, payloadSizeBytes_, slotSizeBytes_, capacity_, actualFileBytes_);
    if (!validA && !validB) {
        setError(StoreError::DefinitionMismatch, StoreState::StructuralFault);
        return false;
    }
    const bool useA = validA && (!validB || recordStoreSequenceNewerOrEqual(recordStoreReadU32(headerA + 32), recordStoreReadU32(headerB + 32)));
    const uint8_t* active = useA ? headerA : headerB;
    activeHeader_ = useA ? 0 : 1;
    firstVisibleId_ = recordStoreReadU32(active + 28);
    headerSequence_ = recordStoreReadU32(active + 32);
    if (!scanRecords()) {
        return false;
    }
    ESP32BASE_LOG_I("record_store",
                    "ready path=%s max_bytes=%lu actual_bytes=%lu slot_bytes=%lu capacity=%lu count=%lu damaged=%lu free_bytes=%lu",
                    path_, static_cast<unsigned long>(maximumFileBytes_),
                    static_cast<unsigned long>(actualFileBytes_),
                    static_cast<unsigned long>(slotSizeBytes_),
                    static_cast<unsigned long>(capacity_),
                    static_cast<unsigned long>(recordCount_),
                    static_cast<unsigned long>(damagedRecordCount_),
                    static_cast<unsigned long>(Esp32BaseFs::freeBytes()));
    return true;
}

bool Esp32BaseRecordStore::scanRecords() {
    Esp32BaseLongOperation::LongOperationScope scope;
    RecordMaximumScanContext maximumScan = {firstVisibleId_, capacity_, 0, 0};
    if (!recordStoreVisitSlots(path_, slotSizeBytes_, payloadSizeBytes_, capacity_,
                               0, capacity_, false, nullptr,
                               recordStoreFindMaximum, &maximumScan)) {
        setError(StoreError::ReadFailed, StoreState::StructuralFault);
        return false;
    }
    uint32_t maximumId = maximumScan.maximumValidId;
    const uint32_t nextAfterValid = maximumId == 0 ? firstVisibleId_ :
                                    (maximumId == UINT32_MAX ? 0 : maximumId + 1U);
    if (maximumId == 0 && maximumScan.minimumCorruptId != 0) {
        maximumId = maximumScan.minimumCorruptId;
    } else if (nextAfterValid != 0 && maximumScan.minimumCorruptId == nextAfterValid) {
        maximumId = nextAfterValid;
    }

    const uint32_t rollingFloor = maximumId > capacity_ ? maximumId - capacity_ + 1U : 1U;
    const uint32_t visibleFloor = rollingFloor > firstVisibleId_ ? rollingFloor : firstVisibleId_;
    RecordStatusScanContext statusScan = {capacity_, firstVisibleId_, visibleFloor, maximumId, 0, 0, 0};
    if (!recordStoreVisitSlots(path_, slotSizeBytes_, payloadSizeBytes_, capacity_,
                               0, capacity_, false, nullptr,
                               recordStoreCollectStatus, &statusScan)) {
        setError(StoreError::ReadFailed, StoreState::StructuralFault);
        return false;
    }
    recordCount_ = statusScan.recordCount;
    damagedRecordCount_ = statusScan.damagedRecordCount;
    oldestRecordId_ = statusScan.oldestRecordId;
    newestRecordId_ = maximumId;
    nextRecordId_ = maximumId == UINT32_MAX ? 0 : (maximumId ? maximumId + 1U : firstVisibleId_);
    error_ = nextRecordId_ == 0 ? StoreError::IdExhausted : StoreError::None;
    state_ = nextRecordId_ == 0 ? StoreState::WriteFault :
             (damagedRecordCount_ > 0 ? StoreState::Degraded : StoreState::Ready);
    return true;
}

bool Esp32BaseRecordStore::captureStartTime(RecordStartTime& startTime) const {
    if (!isReady()) {
        return false;
    }
    const Esp32BaseTime::Snapshot now = Esp32BaseTime::snapshot();
    if (now.bootId == 0) {
        return false;
    }
    startTime.bootId = now.bootId;
    startTime.uptimeSec = now.uptimeSec;
    return true;
}

bool Esp32BaseRecordStore::appendInstant(const uint8_t* payload, size_t payloadSizeBytes) {
    return appendWithDuration(0, payload, payloadSizeBytes);
}

bool Esp32BaseRecordStore::appendCompleted(const RecordStartTime& startTime,
                                           const uint8_t* payload,
                                           size_t payloadSizeBytes) {
    const Esp32BaseTime::Snapshot now = Esp32BaseTime::snapshot();
    if (startTime.bootId == 0 || startTime.bootId != now.bootId || startTime.uptimeSec > now.uptimeSec) {
        error_ = StoreError::InvalidStartTime;
        return false;
    }
    return appendWithDuration(now.uptimeSec - startTime.uptimeSec, payload, payloadSizeBytes);
}

bool Esp32BaseRecordStore::appendWithDuration(uint32_t durationSec,
                                              const uint8_t* payload,
                                              size_t payloadSizeBytes) {
    if (!isReady() || state_ == StoreState::WriteFault || !payload || payloadSizeBytes != payloadSizeBytes_) {
        if (!payload || payloadSizeBytes != payloadSizeBytes_) {
            error_ = StoreError::InvalidPayload;
        }
        return false;
    }
    if (nextRecordId_ == 0) {
        setError(StoreError::IdExhausted, StoreState::WriteFault);
        return false;
    }
    if (minimumFileSystemFreeBytes_ > 0 && Esp32BaseFs::freeBytes() < minimumFileSystemFreeBytes_) {
        setError(StoreError::InsufficientFreeSpace, StoreState::WriteFault);
        return false;
    }
    const Esp32BaseTime::Snapshot now = Esp32BaseTime::snapshot();
    if (now.bootId == 0 || durationSec > now.uptimeSec) {
        error_ = StoreError::InvalidStartTime;
        return false;
    }

    uint8_t metadata[kRecordStoreMetadataBytes];
    recordStoreWriteU32(metadata, nextRecordId_);
    recordStoreWriteU32(metadata + 4, now.synced ? now.epochSec : 0);
    recordStoreWriteU32(metadata + 8, now.bootId);
    recordStoreWriteU32(metadata + 12, now.uptimeSec);
    recordStoreWriteU32(metadata + 16, durationSec);
    uint32_t crc = recordStoreCrcUpdate(0xFFFFFFFFUL, metadata, sizeof(metadata));
    crc = ~recordStoreCrcUpdate(crc, payload, payloadSizeBytes);
    uint8_t crcBytes[4];
    recordStoreWriteU32(crcBytes, crc);

    const uint32_t recordId = nextRecordId_;
    const uint32_t slot = (recordId - 1U) % capacity_;
    RecordSlotData replaced = {};
    const RecordSlotState replacedState = recordStoreReadSlot(path_, slotSizeBytes_, payloadSizeBytes_, capacity_, slot, nullptr, replaced);
    if (replacedState == RECORD_SLOT_IO_ERROR) {
        setError(StoreError::ReadFailed, StoreState::WriteFault);
        return false;
    }
    const uint32_t oldRollingFloor = newestRecordId_ > capacity_ ? newestRecordId_ - capacity_ + 1U : 1U;
    const uint32_t oldVisibleFloor = oldRollingFloor > firstVisibleId_ ? oldRollingFloor : firstVisibleId_;
    const bool replacedCurrentValid = replacedState == RECORD_SLOT_VALID &&
                                      replaced.recordId >= oldVisibleFloor &&
                                      replaced.recordId <= newestRecordId_;
    const esp32base_internal::FsWriteSegment segments[] = {
        {metadata, sizeof(metadata)},
        {payload, payloadSizeBytes},
        {crcBytes, sizeof(crcBytes)}
    };
    if (!esp32base_internal::fsWriteSegmentsAt(path_, recordStoreSlotOffset(slotSizeBytes_, slot), segments, 3)) {
        setError(StoreError::WriteFailed, StoreState::WriteFault);
        return false;
    }
    RecordSlotData verify = {};
    const RecordSlotState verifyResult = recordStoreReadSlot(path_, slotSizeBytes_, payloadSizeBytes_, capacity_, slot, nullptr, verify);
    if (verifyResult != RECORD_SLOT_VALID || verify.recordId != recordId) {
        setError(StoreError::VerifyFailed, StoreState::WriteFault);
        return false;
    }

    newestRecordId_ = recordId;
    nextRecordId_ = recordId == UINT32_MAX ? 0 : recordId + 1U;
    if (!replacedCurrentValid && recordCount_ < capacity_) {
        ++recordCount_;
    }
    if (damagedRecordCount_ > 0 || replacedState == RECORD_SLOT_CORRUPT) {
        if (!scanRecords()) {
            return false;
        }
    } else {
        const uint32_t rollingFloor = newestRecordId_ > capacity_ ? newestRecordId_ - capacity_ + 1U : 1U;
        const uint32_t visibleFloor = rollingFloor > firstVisibleId_ ? rollingFloor : firstVisibleId_;
        oldestRecordId_ = recordCount_ == 0 ? 0 : visibleFloor;
        error_ = nextRecordId_ == 0 ? StoreError::IdExhausted : StoreError::None;
        state_ = nextRecordId_ == 0 ? StoreState::WriteFault : StoreState::Ready;
    }
    return true;
}

bool Esp32BaseRecordStore::readLatest(uint32_t offset,
                                      uint32_t limit,
                                      uint8_t* scratchBuffer,
                                      size_t scratchBufferBytes,
                                      ReadCallback callback,
                                      void* user) {
    if (!isReady() || !scratchBuffer || scratchBufferBytes < payloadSizeBytes_ || !callback || limit == 0) {
        return false;
    }
    const uint32_t rollingFloor = newestRecordId_ > capacity_ ? newestRecordId_ - capacity_ + 1U : 1U;
    const uint32_t visibleFloor = rollingFloor > firstVisibleId_ ? rollingFloor : firstVisibleId_;
    if (newestRecordId_ == 0 || newestRecordId_ < visibleFloor) return true;
    const uint32_t slotCount = newestRecordId_ - visibleFloor + 1U;
    RecordLatestReadContext context = {newestRecordId_, offset, limit, 0, 0,
                                       payloadSizeBytes_, scratchBuffer, callback, user, false};
    const bool ok = recordStoreVisitSlots(path_, slotSizeBytes_, payloadSizeBytes_, capacity_,
                                          (newestRecordId_ - 1U) % capacity_,
                                          slotCount,
                                          true,
                                          scratchBuffer,
                                          recordStoreReadLatestSlot,
                                          &context);
    if (!ok && !context.completed) {
        setError(StoreError::ReadFailed, state_ == StoreState::StructuralFault ? state_ : StoreState::WriteFault);
        return false;
    }
    return true;
}

Esp32BaseRecordStore::RecordReadResult Esp32BaseRecordStore::readById(uint32_t recordId,
                                                                      uint8_t* payloadOut,
                                                                      size_t payloadOutBytes,
                                                                      RecordMetadata& recordOut) {
    recordOut = RecordMetadata();
    if (!isReady() || recordId == 0 || !payloadOut || payloadOutBytes < payloadSizeBytes_) {
        return RecordReadResult::InvalidArgument;
    }
    const uint32_t rollingFloor = newestRecordId_ > capacity_ ? newestRecordId_ - capacity_ + 1U : 1U;
    const uint32_t visibleFloor = rollingFloor > firstVisibleId_ ? rollingFloor : firstVisibleId_;
    if (recordId < visibleFloor || recordId > newestRecordId_) {
        return RecordReadResult::NotFound;
    }
    const uint32_t slot = (recordId - 1U) % capacity_;
    RecordSlotData record = {};
    const RecordSlotState result = recordStoreReadSlot(path_, slotSizeBytes_, payloadSizeBytes_, capacity_, slot, payloadOut, record);
    if (result == RECORD_SLOT_IO_ERROR) {
        return RecordReadResult::IoError;
    }
    if (result == RECORD_SLOT_CORRUPT) {
        return RecordReadResult::Corrupt;
    }
    if (result != RECORD_SLOT_VALID || record.recordId != recordId) {
        return RecordReadResult::NotFound;
    }
    recordOut.recordId = record.recordId;
    recordOut.timing = record.timing;
    return RecordReadResult::Found;
}

bool Esp32BaseRecordStore::writeHeader(uint8_t copyIndex, uint32_t firstVisibleId, uint32_t sequence) {
    uint8_t header[kRecordStoreHeaderBytes];
    recordStoreBuildHeader(header, storeVersion_, payloadSizeBytes_, slotSizeBytes_, capacity_, actualFileBytes_, firstVisibleId, sequence);
    if (!Esp32BaseFs::writeBytesAt(path_, static_cast<uint32_t>(copyIndex) * kRecordStoreHeaderBytes, header, sizeof(header))) {
        return false;
    }
    uint8_t verify[kRecordStoreHeaderBytes];
    return recordStoreReadHeader(path_, copyIndex, verify) &&
           recordStoreHeaderMatches(verify, storeVersion_, payloadSizeBytes_, slotSizeBytes_, capacity_, actualFileBytes_) &&
           recordStoreReadU32(verify + 28) == firstVisibleId &&
           recordStoreReadU32(verify + 32) == sequence;
}

bool Esp32BaseRecordStore::clear() {
    if (!isReady()) {
        return false;
    }
    const uint8_t nextHeader = activeHeader_ == 0 ? 1 : 0;
    const uint32_t clearBoundary = nextRecordId_ ? nextRecordId_ : UINT32_MAX;
    if (!writeHeader(nextHeader, clearBoundary, headerSequence_ + 1U)) {
        setError(StoreError::WriteFailed, StoreState::WriteFault);
        return false;
    }
    activeHeader_ = nextHeader;
    headerSequence_ += 1U;
    firstVisibleId_ = clearBoundary;
    recordCount_ = 0;
    damagedRecordCount_ = 0;
    oldestRecordId_ = 0;
    newestRecordId_ = 0;
    error_ = nextRecordId_ == 0 ? StoreError::IdExhausted : StoreError::None;
    state_ = nextRecordId_ == 0 ? StoreState::WriteFault : StoreState::Ready;
    return true;
}

bool Esp32BaseRecordStore::readStatus(StoreStatus& status) const {
    status = StoreStatus();
    status.state = state_;
    status.error = error_;
    status.ready = isReady();
    status.writable = isWritable();
    status.recordCount = recordCount_;
    status.capacity = capacity_;
    status.damagedRecordCount = damagedRecordCount_;
    status.oldestRecordId = oldestRecordId_;
    status.newestRecordId = newestRecordId_;
    status.nextRecordId = nextRecordId_;
    status.slotSizeBytes = slotSizeBytes_;
    status.actualFileBytes = actualFileBytes_;
    status.maximumFileBytes = maximumFileBytes_;
    status.fileSystemTotalBytes = Esp32BaseFs::totalBytes();
    status.fileSystemUsedBytes = Esp32BaseFs::usedBytes();
    status.fileSystemFreeBytes = Esp32BaseFs::freeBytes();
    status.path = path_;
    status.errorReason = storeErrorName(error_);
    return definitionSet_;
}

bool Esp32BaseRecordStore::isReady() const {
    return state_ == StoreState::Ready || state_ == StoreState::Degraded || state_ == StoreState::WriteFault;
}

bool Esp32BaseRecordStore::isWritable() const {
    return state_ == StoreState::Ready || state_ == StoreState::Degraded;
}

Esp32BaseRecordStore::StoreState Esp32BaseRecordStore::state() const { return state_; }
Esp32BaseRecordStore::StoreError Esp32BaseRecordStore::lastError() const { return error_; }
const char* Esp32BaseRecordStore::lastErrorReason() const { return storeErrorName(error_); }
const char* Esp32BaseRecordStore::path() const { return path_; }

bool Esp32BaseRecordStore::resolveCompletedEpoch(const RecordTiming& timing, uint32_t& epochSec) {
    epochSec = 0;
    if (timing.completedEpochSec != 0) {
        epochSec = timing.completedEpochSec;
        return true;
    }
    return Esp32BaseTime::resolveCurrentBootEvent(timing.completedBootId, timing.completedUptimeSec, &epochSec);
}

bool Esp32BaseRecordStore::resolveStartedEpoch(const RecordTiming& timing, uint32_t& epochSec) {
    uint32_t completed = 0;
    if (!resolveCompletedEpoch(timing, completed) || completed < timing.durationSec) {
        epochSec = 0;
        return false;
    }
    epochSec = completed - timing.durationSec;
    return true;
}

const char* Esp32BaseRecordStore::storeStateName(StoreState state) {
    switch (state) {
        case StoreState::Uninitialized: return "uninitialized";
        case StoreState::Ready: return "ready";
        case StoreState::Degraded: return "degraded";
        case StoreState::WriteFault: return "write_fault";
        case StoreState::StructuralFault: return "structural_fault";
        default: return "unknown";
    }
}

const char* Esp32BaseRecordStore::storeErrorName(StoreError error) {
    switch (error) {
        case StoreError::None: return "none";
        case StoreError::FileSystemUnavailable: return "fs_unavailable";
        case StoreError::InvalidDefinition: return "invalid_definition";
        case StoreError::InsufficientFreeSpace: return "insufficient_free_space";
        case StoreError::DirectoryCreateFailed: return "directory_create_failed";
        case StoreError::FileCreateFailed: return "file_create_failed";
        case StoreError::FileRenameFailed: return "file_rename_failed";
        case StoreError::FileSizeMismatch: return "file_size_mismatch";
        case StoreError::HeaderInvalid: return "header_invalid";
        case StoreError::DefinitionMismatch: return "definition_mismatch";
        case StoreError::ReadFailed: return "read_failed";
        case StoreError::WriteFailed: return "write_failed";
        case StoreError::VerifyFailed: return "verify_failed";
        case StoreError::InvalidStartTime: return "invalid_start_time";
        case StoreError::InvalidPayload: return "invalid_payload";
        case StoreError::IdExhausted: return "id_exhausted";
        default: return "unknown";
    }
}

#endif
