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

constexpr const char* kEsp32BaseDirectory = "/esp32base";
constexpr const char* kRecordStoreDirectory = "/esp32base/records";
constexpr const char* kControlFileName = "control.bin";
constexpr const char* kControlTempFileName = "control.tmp";
constexpr uint32_t kControlMagic = 0x4C544352UL; // RCTL
constexpr uint32_t kSegmentMagic = 0x47455352UL; // RSEG
constexpr uint16_t kContainerVersion = 1;
constexpr uint32_t kControlHeaderBytes = 64;
constexpr uint32_t kControlHeaderCopies = 2;
constexpr uint32_t kControlFileBytes = kControlHeaderBytes * kControlHeaderCopies;
constexpr uint32_t kSegmentHeaderBytes = 32;
constexpr uint32_t kRecordMetadataBytes = 20;
constexpr uint32_t kRecordCrcBytes = 4;
constexpr uint32_t kLittleFsBlockBytes = 4096;
constexpr uint32_t kTargetMaximumSegments = 16;
constexpr size_t kMaximumDiscoveredSegments = 40;

enum RecordSlotState : uint8_t {
    RECORD_SLOT_VALID,
    RECORD_SLOT_CORRUPT,
    RECORD_SLOT_IO_ERROR
};

struct RecordSlotData {
    uint32_t recordId;
    Esp32BaseRecordStore::RecordTiming timing;
    uint32_t storedCrc;
    uint32_t calculatedCrc;
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
    if (!name || !name[0]) return false;
    const size_t length = strlen(name);
    if (length == 0 || length > Esp32BaseRecordStore::MAX_RECORD_TYPE_NAME_LENGTH ||
        name[0] == '-' || name[length - 1] == '-') {
        return false;
    }
    for (const char* p = name; *p; ++p) {
        const char c = *p;
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-') continue;
        return false;
    }
    return true;
}

uint32_t recordStorePopcount(uint32_t value) {
    uint32_t count = 0;
    while (value) {
        value &= value - 1U;
        ++count;
    }
    return count;
}

uint32_t recordStoreCtzBlockIndex(uint32_t offset) {
    const uint32_t dataBase = kLittleFsBlockBytes - 2U * sizeof(uint32_t);
    uint32_t rough = offset / dataBase;
    if (rough == 0) return 0;
    rough = (offset - sizeof(uint32_t) * (recordStorePopcount(rough - 1U) + 2U)) / dataBase;
    return rough;
}

uint32_t recordStoreCtzFileLimit(uint32_t blockCount) {
    if (blockCount == 0 || blockCount > UINT32_MAX / kLittleFsBlockBytes) return 0;
    uint32_t limit = blockCount * kLittleFsBlockBytes;
    while (limit > 0 && recordStoreCtzBlockIndex(limit - 1U) >= blockCount) --limit;
    return limit;
}

uint32_t recordStoreChooseSegmentLimit(uint32_t maximumStoreBytes, uint32_t slotSize) {
    // Product-sized stores use 8-16 segments in the common 32-512 KiB range.
    // This keeps startup/file counts bounded without making one rotation discard
    // one eighth of a large 512 KiB history. Only power-of-two block counts are
    // selected so the CTZ boundary remains predictable on LittleFS.
    uint32_t preferredBlocks = 1;
    if (maximumStoreBytes > 48UL * 1024UL) preferredBlocks = 2;   // 8 KiB
    if (maximumStoreBytes > 96UL * 1024UL) preferredBlocks = 4;   // 16 KiB
    if (maximumStoreBytes > 192UL * 1024UL) preferredBlocks = 8;  // 32 KiB

    uint32_t blocks = preferredBlocks;
    while (true) {
        const uint32_t limit = recordStoreCtzFileLimit(blocks);
        if (limit >= kSegmentHeaderBytes + slotSize) {
            const uint32_t usableBudget = maximumStoreBytes - kControlFileBytes;
            const uint32_t slotsPerFullSegment =
                (limit - kSegmentHeaderBytes) / slotSize;
            const uint32_t fullSegmentBytes =
                kSegmentHeaderBytes + slotsPerFullSegment * slotSize;
            const uint32_t segmentEstimate =
                (usableBudget + fullSegmentBytes - 1U) / fullSegmentBytes;
            if (segmentEstimate <= kTargetMaximumSegments) return limit;
        }
        if (blocks > (UINT32_MAX / kLittleFsBlockBytes) / 2U) return 0;
        blocks *= 2U;
    }
}

uint32_t recordStoreCalculateCapacity(uint32_t maximumStoreBytes,
                                      uint32_t segmentFileLimit,
                                      uint32_t slotSize) {
    if (maximumStoreBytes < kControlFileBytes + kSegmentHeaderBytes + slotSize ||
        segmentFileLimit < kSegmentHeaderBytes + slotSize) {
        return 0;
    }
    const uint32_t fullSegmentCapacity = (segmentFileLimit - kSegmentHeaderBytes) / slotSize;
    const uint32_t fullSegmentBytes = kSegmentHeaderBytes + fullSegmentCapacity * slotSize;
    uint32_t remaining = maximumStoreBytes - kControlFileBytes;
    uint64_t capacity = 0;
    size_t segments = 0;
    while (remaining >= kSegmentHeaderBytes + slotSize && segments < kMaximumDiscoveredSegments) {
        const uint32_t slots = remaining >= fullSegmentBytes
            ? fullSegmentCapacity
            : (remaining - kSegmentHeaderBytes) / slotSize;
        if (slots == 0) break;
        capacity += slots;
        if (capacity > UINT32_MAX) return 0;
        remaining -= kSegmentHeaderBytes + slots * slotSize;
        ++segments;
    }
    return static_cast<uint32_t>(capacity);
}

void recordStoreBuildControlHeader(uint8_t* header,
                                   uint16_t storeVersion,
                                   uint32_t payloadSize,
                                   uint32_t slotSize,
                                   uint32_t firstVisibleId,
                                   uint32_t nextRecordIdHint,
                                   uint32_t sequence) {
    memset(header, 0, kControlHeaderBytes);
    recordStoreWriteU32(header, kControlMagic);
    recordStoreWriteU16(header + 4, kContainerVersion);
    recordStoreWriteU16(header + 6, static_cast<uint16_t>(kControlHeaderBytes));
    recordStoreWriteU16(header + 8, storeVersion);
    recordStoreWriteU32(header + 12, payloadSize);
    recordStoreWriteU32(header + 16, slotSize);
    recordStoreWriteU32(header + 20, firstVisibleId ? firstVisibleId : 1U);
    recordStoreWriteU32(header + 24, sequence);
    recordStoreWriteU32(header + 28, nextRecordIdHint);
    recordStoreWriteU32(header + 32, 0);
    recordStoreWriteU32(header + 32, recordStoreCrc(header, kControlHeaderBytes));
}

bool recordStoreHeaderCrcValid(const uint8_t* header, size_t length, size_t crcOffset) {
    if (!header || length > kControlHeaderBytes || crcOffset + 4U > length) return false;
    uint8_t copy[kControlHeaderBytes];
    memcpy(copy, header, length);
    const uint32_t stored = recordStoreReadU32(copy + crcOffset);
    recordStoreWriteU32(copy + crcOffset, 0);
    return stored == recordStoreCrc(copy, length);
}

bool recordStoreControlHeaderValid(const uint8_t* header) {
    return recordStoreReadU32(header) == kControlMagic &&
           recordStoreReadU16(header + 4) == kContainerVersion &&
           recordStoreReadU16(header + 6) == kControlHeaderBytes &&
           recordStoreReadU16(header + 10) == 0 &&
           recordStoreReadU32(header + 20) != 0 &&
           recordStoreHeaderCrcValid(header, kControlHeaderBytes, 32);
}

bool recordStoreControlHeaderMatches(const uint8_t* header,
                                     uint16_t storeVersion,
                                     uint32_t payloadSize,
                                     uint32_t slotSize) {
    return recordStoreControlHeaderValid(header) &&
           recordStoreReadU16(header + 8) == storeVersion &&
           recordStoreReadU32(header + 12) == payloadSize &&
           recordStoreReadU32(header + 16) == slotSize;
}

bool recordStoreSequenceNewerOrEqual(uint32_t left, uint32_t right) {
    return static_cast<int32_t>(left - right) >= 0;
}

void recordStoreBuildSegmentHeader(uint8_t* header,
                                   uint16_t storeVersion,
                                   uint32_t payloadSize,
                                   uint32_t slotSize,
                                   uint32_t firstRecordId,
                                   uint32_t fileLimitBytes) {
    memset(header, 0, kSegmentHeaderBytes);
    recordStoreWriteU32(header, kSegmentMagic);
    recordStoreWriteU16(header + 4, kContainerVersion);
    recordStoreWriteU16(header + 6, static_cast<uint16_t>(kSegmentHeaderBytes));
    recordStoreWriteU16(header + 8, storeVersion);
    recordStoreWriteU32(header + 12, payloadSize);
    recordStoreWriteU32(header + 16, slotSize);
    recordStoreWriteU32(header + 20, firstRecordId);
    recordStoreWriteU32(header + 24, fileLimitBytes);
    recordStoreWriteU32(header + 28, 0);
    recordStoreWriteU32(header + 28, recordStoreCrc(header, kSegmentHeaderBytes));
}

bool recordStoreSegmentHeaderMatches(const uint8_t* header,
                                     uint16_t storeVersion,
                                     uint32_t payloadSize,
                                     uint32_t slotSize,
                                     uint32_t firstRecordId,
                                     uint32_t fileBytes,
                                     uint32_t& fileLimitBytes) {
    fileLimitBytes = recordStoreReadU32(header + 24);
    return recordStoreReadU32(header) == kSegmentMagic &&
           recordStoreReadU16(header + 4) == kContainerVersion &&
           recordStoreReadU16(header + 6) == kSegmentHeaderBytes &&
           recordStoreReadU16(header + 8) == storeVersion &&
           recordStoreReadU16(header + 10) == 0 &&
           recordStoreReadU32(header + 12) == payloadSize &&
           recordStoreReadU32(header + 16) == slotSize &&
           recordStoreReadU32(header + 20) == firstRecordId &&
           fileLimitBytes >= kSegmentHeaderBytes + slotSize &&
           fileBytes >= kSegmentHeaderBytes && fileBytes <= fileLimitBytes &&
           recordStoreHeaderCrcValid(header, kSegmentHeaderBytes, 28);
}

bool recordStoreFormatSegmentPath(const char* root,
                                  uint32_t firstRecordId,
                                  const char* extension,
                                  char* out,
                                  size_t outSize) {
    return root && extension && out &&
           snprintf(out, outSize, "%s/%08lx%s", root,
                    static_cast<unsigned long>(firstRecordId), extension) > 0 &&
           strlen(out) < outSize;
}

const char* recordStoreBaseName(const char* path) {
    if (!path) return "";
    const char* slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

bool recordStoreParseHexId(const char* name, const char* extension, uint32_t& id) {
    id = 0;
    if (!name || !extension || strlen(name) != 8U + strlen(extension) ||
        strcmp(name + 8, extension) != 0) {
        return false;
    }
    uint32_t value = 0;
    for (size_t i = 0; i < 8; ++i) {
        const char c = name[i];
        uint8_t digit = 0;
        if (c >= '0' && c <= '9') digit = static_cast<uint8_t>(c - '0');
        else if (c >= 'a' && c <= 'f') digit = static_cast<uint8_t>(c - 'a' + 10);
        else return false;
        value = (value << 4) | digit;
    }
    if (value == 0) return false;
    id = value;
    return true;
}

struct DiscoveredSegment {
    uint32_t firstRecordId;
    uint32_t fileBytes;
};

struct SegmentDiscovery {
    DiscoveredSegment segments[kMaximumDiscoveredSegments];
    uint32_t temporaryIds[kMaximumDiscoveredSegments];
    size_t count;
    size_t temporaryCount;
    bool unknownEntry;
    bool tooMany;
};

void recordStoreDiscoverEntry(const Esp32BaseFs::EntryInfo& entry, void* user) {
    SegmentDiscovery* discovery = static_cast<SegmentDiscovery*>(user);
    if (!discovery) return;
    const char* name = recordStoreBaseName(entry.name);
    if (!entry.isDir && strcmp(name, kControlFileName) == 0) return;
    if (!entry.isDir && strcmp(name, kControlTempFileName) == 0) return;
    uint32_t id = 0;
    if (!entry.isDir && recordStoreParseHexId(name, ".seg", id)) {
        if (entry.size > UINT32_MAX || discovery->count >= kMaximumDiscoveredSegments) {
            discovery->tooMany = true;
            return;
        }
        discovery->segments[discovery->count++] = {id, static_cast<uint32_t>(entry.size)};
        return;
    }
    if (!entry.isDir && recordStoreParseHexId(name, ".tmp", id)) {
        if (discovery->temporaryCount < kMaximumDiscoveredSegments) {
            discovery->temporaryIds[discovery->temporaryCount++] = id;
        } else {
            discovery->tooMany = true;
        }
        return;
    }
    discovery->unknownEntry = true;
}

void recordStoreSortDiscovered(SegmentDiscovery& discovery) {
    for (size_t i = 1; i < discovery.count; ++i) {
        const DiscoveredSegment value = discovery.segments[i];
        size_t j = i;
        while (j > 0 && discovery.segments[j - 1].firstRecordId > value.firstRecordId) {
            discovery.segments[j] = discovery.segments[j - 1];
            --j;
        }
        discovery.segments[j] = value;
    }
}

struct RecordVisitContext {
    uint32_t position;
    uint32_t slotSize;
    uint32_t payloadSize;
    uint32_t expectedRecordId;
    uint32_t crc;
    uint8_t metadata[kRecordMetadataBytes];
    uint8_t crcBytes[kRecordCrcBytes];
    uint8_t* payloadOut;
};

bool recordStoreVisitRecordBytes(const uint8_t* data, size_t length, void* user) {
    RecordVisitContext* context = static_cast<RecordVisitContext*>(user);
    if (!context || (!data && length > 0)) return false;
    for (size_t i = 0; i < length; ++i, ++context->position) {
        const uint8_t value = data[i];
        if (context->position < context->slotSize - kRecordCrcBytes) {
            context->crc = recordStoreCrcUpdate(context->crc, &value, 1);
        }
        if (context->position < kRecordMetadataBytes) {
            context->metadata[context->position] = value;
        } else if (context->position < kRecordMetadataBytes + context->payloadSize) {
            if (context->payloadOut) {
                context->payloadOut[context->position - kRecordMetadataBytes] = value;
            }
        } else if (context->position < context->slotSize) {
            context->crcBytes[context->position - (context->slotSize - kRecordCrcBytes)] = value;
        }
    }
    return true;
}

RecordSlotState recordStoreFinishRecord(const RecordVisitContext& context, RecordSlotData& out) {
    memset(&out, 0, sizeof(out));
    out.recordId = recordStoreReadU32(context.metadata);
    out.timing.completedEpochSec = recordStoreReadU32(context.metadata + 4);
    out.timing.completedBootId = recordStoreReadU32(context.metadata + 8);
    out.timing.completedUptimeSec = recordStoreReadU32(context.metadata + 12);
    out.timing.durationSec = recordStoreReadU32(context.metadata + 16);
    out.storedCrc = recordStoreReadU32(context.crcBytes);
    out.calculatedCrc = ~context.crc;
    if (out.recordId != context.expectedRecordId || out.timing.completedBootId == 0 ||
        out.timing.durationSec > out.timing.completedUptimeSec ||
        out.storedCrc != out.calculatedCrc) {
        return RECORD_SLOT_CORRUPT;
    }
    return RECORD_SLOT_VALID;
}

RecordSlotState recordStoreReadRecord(const char* path,
                                      uint32_t offset,
                                      uint32_t slotSize,
                                      uint32_t payloadSize,
                                      uint32_t expectedRecordId,
                                      uint8_t* payloadOut,
                                      RecordSlotData& out) {
    RecordVisitContext context = {};
    context.slotSize = slotSize;
    context.payloadSize = payloadSize;
    context.expectedRecordId = expectedRecordId;
    context.crc = 0xFFFFFFFFUL;
    context.payloadOut = payloadOut;
    if (!esp32base_internal::fsVisitBytesAt(path, offset, slotSize,
                                            recordStoreVisitRecordBytes, &context)) {
        return RECORD_SLOT_IO_ERROR;
    }
    return recordStoreFinishRecord(context, out);
}

using RecordSlotVisitor = bool (*)(uint32_t slot,
                                   RecordSlotState state,
                                   const RecordSlotData& record,
                                   void* user);

struct RecordSlotsVisitContext {
    uint32_t firstRecordId;
    uint32_t slotSize;
    uint32_t payloadSize;
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
    if (!context || !context->visitor || slotByteOffset + length > context->slotSize ||
        context->firstRecordId > UINT32_MAX - slot) {
        return false;
    }
    if (slotByteOffset == 0) {
        memset(&context->recordVisit, 0, sizeof(context->recordVisit));
        context->recordVisit.slotSize = context->slotSize;
        context->recordVisit.payloadSize = context->payloadSize;
        context->recordVisit.expectedRecordId = context->firstRecordId + slot;
        context->recordVisit.crc = 0xFFFFFFFFUL;
        context->recordVisit.payloadOut = context->payloadOut;
    }
    if (context->recordVisit.position != slotByteOffset ||
        !recordStoreVisitRecordBytes(data, length, &context->recordVisit)) {
        return false;
    }
    if (context->recordVisit.position == context->slotSize) {
        RecordSlotData record = {};
        const RecordSlotState state = recordStoreFinishRecord(context->recordVisit, record);
        return context->visitor(slot, state, record, context->user);
    }
    return true;
}

bool recordStoreVisitSegmentSlots(const char* path,
                                  uint32_t firstRecordId,
                                  uint32_t slotSize,
                                  uint32_t payloadSize,
                                  uint32_t fullSlots,
                                  bool descending,
                                  uint8_t* payloadOut,
                                  RecordSlotVisitor visitor,
                                  void* user) {
    if (fullSlots == 0) return true;
    RecordSlotsVisitContext context = {};
    context.firstRecordId = firstRecordId;
    context.slotSize = slotSize;
    context.payloadSize = payloadSize;
    context.payloadOut = payloadOut;
    context.visitor = visitor;
    context.user = user;
    return esp32base_internal::fsVisitFixedSlots(path,
                                                 kSegmentHeaderBytes,
                                                 slotSize,
                                                 fullSlots,
                                                 descending ? fullSlots - 1U : 0U,
                                                 fullSlots,
                                                 descending,
                                                 recordStoreVisitFixedSlot,
                                                 &context);
}

struct SegmentScanContext {
    uint32_t firstRecordId;
    uint32_t firstVisibleId;
    uint32_t validCount;
    uint32_t damagedCount;
    uint32_t oldestValidId;
    uint32_t newestValidId;
};

bool recordStoreCollectSegmentRecord(uint32_t slot,
                                     RecordSlotState state,
                                     const RecordSlotData& record,
                                     void* user) {
    SegmentScanContext* context = static_cast<SegmentScanContext*>(user);
    if (!context || context->firstRecordId > UINT32_MAX - slot) return false;
    const uint32_t expectedId = context->firstRecordId + slot;
    if (expectedId < context->firstVisibleId) return true;
    if (state == RECORD_SLOT_VALID && record.recordId == expectedId) {
        ++context->validCount;
        if (context->oldestValidId == 0) context->oldestValidId = expectedId;
        context->newestValidId = expectedId;
    } else {
        ++context->damagedCount;
    }
    return true;
}

struct LatestReadContext {
    uint32_t firstRecordId;
    uint32_t firstVisibleId;
    uint32_t offset;
    uint32_t limit;
    uint32_t matched;
    uint32_t emitted;
    uint32_t payloadSize;
    uint8_t* payload;
    Esp32BaseRecordStore::ReadCallback callback;
    void* user;
    bool completed;
};

bool recordStoreReadLatestSlot(uint32_t slot,
                               RecordSlotState state,
                               const RecordSlotData& record,
                               void* user) {
    LatestReadContext* context = static_cast<LatestReadContext*>(user);
    if (!context || context->firstRecordId > UINT32_MAX - slot) return false;
    const uint32_t expectedId = context->firstRecordId + slot;
    if (expectedId < context->firstVisibleId || state != RECORD_SLOT_VALID ||
        record.recordId != expectedId) {
        return true;
    }
    const uint32_t index = context->matched++;
    if (index >= context->offset && context->emitted < context->limit) {
        Esp32BaseRecordStore::RecordView view;
        view.recordId = record.recordId;
        view.timing = record.timing;
        view.payload = context->payload;
        view.payloadSizeBytes = context->payloadSize;
        context->callback(view, context->user);
        ++context->emitted;
    }
    if (context->emitted >= context->limit) {
        context->completed = true;
        return false;
    }
    return true;
}

} // namespace

Esp32BaseRecordStore::Esp32BaseRecordStore()
    : storeVersion_(0), payloadSizeBytes_(0), maximumStoreBytes_(0),
      minimumFileSystemFreeBytes_(0), slotSizeBytes_(0), capacity_(0),
      segmentFileLimitBytes_(0), currentStoreBytes_(0), firstVisibleId_(1),
      controlNextRecordId_(1), headerSequence_(0), activeHeader_(0), recordCount_(0),
      damagedRecordCount_(0), oldestRecordId_(0), newestRecordId_(0), nextRecordId_(1),
      segmentCount_(0), state_(StoreState::Uninitialized), error_(StoreError::None),
      definitionSet_(false) {
    recordTypeName_[0] = '\0';
    path_[0] = '\0';
    controlPath_[0] = '\0';
    tempPath_[0] = '\0';
}

void Esp32BaseRecordStore::resetRuntime() {
    currentStoreBytes_ = 0;
    firstVisibleId_ = 1;
    controlNextRecordId_ = 1;
    headerSequence_ = 0;
    activeHeader_ = 0;
    recordCount_ = 0;
    damagedRecordCount_ = 0;
    oldestRecordId_ = 0;
    newestRecordId_ = 0;
    nextRecordId_ = 1;
    segmentCount_ = 0;
    memset(segments_, 0, sizeof(segments_));
    state_ = StoreState::Uninitialized;
    error_ = StoreError::None;
}

void Esp32BaseRecordStore::setError(StoreError error, StoreState state) {
    error_ = error;
    state_ = state;
    ESP32BASE_LOG_W("record_store", "fault path=%s state=%s error=%s",
                    path_, storeStateName(state_), storeErrorName(error_));
}

bool Esp32BaseRecordStore::begin(const StoreDefinition& definition) {
    resetRuntime();
    definitionSet_ = false;
    if (!recordStoreTypeNameValid(definition.recordTypeName) || definition.storeVersion == 0 ||
        definition.payloadSizeBytes == 0 || definition.payloadSizeBytes > UINT32_MAX - 24U) {
        setError(StoreError::InvalidDefinition, StoreState::StructuralFault);
        return false;
    }
    const uint32_t slotSize = definition.payloadSizeBytes + 24U;
    if (definition.maximumStoreBytes < kControlFileBytes + kSegmentHeaderBytes + slotSize) {
        setError(StoreError::InvalidDefinition, StoreState::StructuralFault);
        return false;
    }
    const uint32_t segmentLimit = recordStoreChooseSegmentLimit(definition.maximumStoreBytes, slotSize);
    const uint32_t capacity = recordStoreCalculateCapacity(definition.maximumStoreBytes,
                                                           segmentLimit,
                                                           slotSize);
    if (segmentLimit == 0 || capacity == 0) {
        setError(StoreError::InvalidDefinition, StoreState::StructuralFault);
        return false;
    }
    const int pathLength = snprintf(path_, sizeof(path_), "%s/%s.v%u",
                                    kRecordStoreDirectory, definition.recordTypeName,
                                    static_cast<unsigned>(definition.storeVersion));
    if (pathLength <= 0 || static_cast<size_t>(pathLength) >= sizeof(path_) ||
        snprintf(controlPath_, sizeof(controlPath_), "%s/%s", path_, kControlFileName) <= 0 ||
        snprintf(tempPath_, sizeof(tempPath_), "%s/%s", path_, kControlTempFileName) <= 0) {
        setError(StoreError::InvalidDefinition, StoreState::StructuralFault);
        return false;
    }
    strncpy(recordTypeName_, definition.recordTypeName, sizeof(recordTypeName_) - 1U);
    recordTypeName_[sizeof(recordTypeName_) - 1U] = '\0';
    storeVersion_ = definition.storeVersion;
    payloadSizeBytes_ = definition.payloadSizeBytes;
    maximumStoreBytes_ = definition.maximumStoreBytes;
    minimumFileSystemFreeBytes_ = definition.minimumFileSystemFreeBytes;
    slotSizeBytes_ = slotSize;
    capacity_ = capacity;
    segmentFileLimitBytes_ = segmentLimit;
    definitionSet_ = true;
    return loadOrCreate();
}

bool Esp32BaseRecordStore::reload() {
    if (!definitionSet_) return false;
    resetRuntime();
    return loadOrCreate();
}

bool Esp32BaseRecordStore::loadOrCreate() {
    if (!Esp32BaseFs::isReady()) {
        setError(StoreError::FileSystemUnavailable, StoreState::StructuralFault);
        return false;
    }
    return Esp32BaseFs::exists(controlPath_) ? loadExisting() : createNew();
}

bool Esp32BaseRecordStore::createNew() {
    const uint64_t required = static_cast<uint64_t>(kControlFileBytes) + minimumFileSystemFreeBytes_;
    if (required > Esp32BaseFs::freeBytes()) {
        setError(StoreError::InsufficientFreeSpace, StoreState::WriteFault);
        return false;
    }
    if ((!Esp32BaseFs::exists(kEsp32BaseDirectory) && !Esp32BaseFs::mkdir(kEsp32BaseDirectory)) ||
        (!Esp32BaseFs::exists(kRecordStoreDirectory) && !Esp32BaseFs::mkdir(kRecordStoreDirectory)) ||
        (!Esp32BaseFs::exists(path_) && !Esp32BaseFs::mkdir(path_))) {
        setError(StoreError::DirectoryCreateFailed, StoreState::StructuralFault);
        return false;
    }
    SegmentDiscovery discovery = {};
    if (!Esp32BaseFs::listDirInfo(path_, recordStoreDiscoverEntry, &discovery)) {
        setError(StoreError::ReadFailed, StoreState::StructuralFault);
        return false;
    }
    if (discovery.count > 0 || discovery.unknownEntry || discovery.tooMany) {
        setError(StoreError::HeaderInvalid, StoreState::StructuralFault);
        return false;
    }
    Esp32BaseFs::removeFileWithRecovery(tempPath_);
    uint8_t file[kControlFileBytes];
    recordStoreBuildControlHeader(file, storeVersion_, payloadSizeBytes_, slotSizeBytes_, 1, 1, 1);
    recordStoreBuildControlHeader(file + kControlHeaderBytes, storeVersion_, payloadSizeBytes_,
                                  slotSizeBytes_, 1, 1, 0);
    if (!Esp32BaseFs::writeBytes(tempPath_, file, sizeof(file))) {
        Esp32BaseFs::removeFileWithRecovery(tempPath_);
        setError(StoreError::FileCreateFailed, StoreState::StructuralFault);
        return false;
    }
    uint8_t verify[kControlFileBytes];
    size_t readLength = 0;
    if (!Esp32BaseFs::readBytes(tempPath_, verify, sizeof(verify), &readLength) ||
        readLength != sizeof(verify) ||
        !recordStoreControlHeaderMatches(verify, storeVersion_, payloadSizeBytes_, slotSizeBytes_) ||
        !recordStoreControlHeaderMatches(verify + kControlHeaderBytes, storeVersion_,
                                         payloadSizeBytes_, slotSizeBytes_)) {
        Esp32BaseFs::removeFileWithRecovery(tempPath_);
        setError(StoreError::VerifyFailed, StoreState::StructuralFault);
        return false;
    }
    if (!Esp32BaseFs::rename(tempPath_, controlPath_)) {
        Esp32BaseFs::removeFileWithRecovery(tempPath_);
        setError(StoreError::FileRenameFailed, StoreState::StructuralFault);
        return false;
    }
    ESP32BASE_LOG_I("record_store",
                    "created path=%s max_store_bytes=%lu segment_limit=%lu slot_bytes=%lu capacity=%lu",
                    path_, static_cast<unsigned long>(maximumStoreBytes_),
                    static_cast<unsigned long>(segmentFileLimitBytes_),
                    static_cast<unsigned long>(slotSizeBytes_),
                    static_cast<unsigned long>(capacity_));
    return loadExisting();
}

bool Esp32BaseRecordStore::loadExisting() {
    if (Esp32BaseFs::fileSize(controlPath_) != static_cast<int64_t>(kControlFileBytes)) {
        setError(StoreError::FileSizeMismatch, StoreState::StructuralFault);
        return false;
    }
    uint8_t file[kControlFileBytes];
    size_t readLength = 0;
    if (!Esp32BaseFs::readBytes(controlPath_, file, sizeof(file), &readLength) ||
        readLength != sizeof(file)) {
        setError(StoreError::ReadFailed, StoreState::StructuralFault);
        return false;
    }
    const uint8_t* a = file;
    const uint8_t* b = file + kControlHeaderBytes;
    const bool headerValidA = recordStoreControlHeaderValid(a);
    const bool headerValidB = recordStoreControlHeaderValid(b);
    if (!headerValidA && !headerValidB) {
        setError(StoreError::HeaderInvalid, StoreState::StructuralFault);
        return false;
    }
    const bool validA = headerValidA && recordStoreControlHeaderMatches(a, storeVersion_, payloadSizeBytes_, slotSizeBytes_);
    const bool validB = headerValidB && recordStoreControlHeaderMatches(b, storeVersion_, payloadSizeBytes_, slotSizeBytes_);
    if (!validA && !validB) {
        setError(StoreError::DefinitionMismatch, StoreState::StructuralFault);
        return false;
    }
    const bool useA = validA && (!validB || recordStoreSequenceNewerOrEqual(recordStoreReadU32(a + 24),
                                                                            recordStoreReadU32(b + 24)));
    const uint8_t* active = useA ? a : b;
    activeHeader_ = useA ? 0 : 1;
    firstVisibleId_ = recordStoreReadU32(active + 20);
    headerSequence_ = recordStoreReadU32(active + 24);
    controlNextRecordId_ = recordStoreReadU32(active + 28);
    if (!scanSegments()) return false;
    ESP32BASE_LOG_I("record_store",
                    "ready path=%s max_store_bytes=%lu current_store_bytes=%lu segment_limit=%lu segments=%u slot_bytes=%lu capacity=%lu count=%lu damaged=%lu free_bytes=%lu",
                    path_, static_cast<unsigned long>(maximumStoreBytes_),
                    static_cast<unsigned long>(currentStoreBytes_),
                    static_cast<unsigned long>(segmentFileLimitBytes_),
                    static_cast<unsigned>(segmentCount_),
                    static_cast<unsigned long>(slotSizeBytes_),
                    static_cast<unsigned long>(capacity_),
                    static_cast<unsigned long>(recordCount_),
                    static_cast<unsigned long>(damagedRecordCount_),
                    static_cast<unsigned long>(Esp32BaseFs::freeBytes()));
    return true;
}

bool Esp32BaseRecordStore::scanOneSegment(SegmentRuntime& segment) {
    char segmentPath[MAX_STORE_PATH_LENGTH];
    if (!recordStoreFormatSegmentPath(path_, segment.firstRecordId, ".seg",
                                      segmentPath, sizeof(segmentPath))) {
        return false;
    }
    if (segment.fileBytes > kSegmentHeaderBytes) {
        const uint32_t dataBytes = segment.fileBytes - kSegmentHeaderBytes;
        segment.fullSlots = dataBytes / slotSizeBytes_;
        segment.candidateSlots = segment.fullSlots + ((dataBytes % slotSizeBytes_) ? 1U : 0U);
    }
    uint8_t header[kSegmentHeaderBytes];
    size_t readLength = 0;
    uint32_t storedFileLimit = 0;
    segment.headerValid = Esp32BaseFs::readBytesAt(segmentPath, 0, header, sizeof(header), &readLength) &&
                          readLength == sizeof(header) &&
                          recordStoreSegmentHeaderMatches(header, storeVersion_, payloadSizeBytes_,
                                                          slotSizeBytes_, segment.firstRecordId,
                                                          segment.fileBytes, storedFileLimit);
    segment.fileLimitBytes = segment.headerValid ? storedFileLimit : segment.fileBytes;
    segment.sealed = !segment.headerValid ||
                     ((segment.fileBytes - (segment.fileBytes >= kSegmentHeaderBytes ? kSegmentHeaderBytes : segment.fileBytes)) % slotSizeBytes_) != 0 ||
                     segment.fileBytes > UINT32_MAX - slotSizeBytes_ ||
                     segment.fileBytes + slotSizeBytes_ > segment.fileLimitBytes;
    if (!segment.headerValid) {
        // The segment filename reserves its first ID even when a torn or corrupt
        // file is too short to contain a complete candidate slot.
        if (segment.candidateSlots == 0) segment.candidateSlots = 1;
        if (segment.firstRecordId >= firstVisibleId_ ||
            (segment.candidateSlots > 0 && segment.firstRecordId <= UINT32_MAX - (segment.candidateSlots - 1U) &&
             segment.firstRecordId + segment.candidateSlots - 1U >= firstVisibleId_)) {
            segment.damagedCount = segment.candidateSlots ? segment.candidateSlots : 1U;
        }
        return true;
    }
    SegmentScanContext context = {segment.firstRecordId, firstVisibleId_, 0, 0, 0, 0};
    if (!recordStoreVisitSegmentSlots(segmentPath, segment.firstRecordId, slotSizeBytes_,
                                      payloadSizeBytes_, segment.fullSlots, false, nullptr,
                                      recordStoreCollectSegmentRecord, &context)) {
        return false;
    }
    segment.validCount = context.validCount;
    segment.damagedCount = context.damagedCount;
    segment.oldestValidId = context.oldestValidId;
    segment.newestValidId = context.newestValidId;
    if (segment.candidateSlots > segment.fullSlots &&
        segment.firstRecordId <= UINT32_MAX - segment.fullSlots &&
        segment.firstRecordId + segment.fullSlots >= firstVisibleId_) {
        ++segment.damagedCount;
    }
    return true;
}

bool Esp32BaseRecordStore::scanSegments() {
    Esp32BaseLongOperation::LongOperationScope scope;
    SegmentDiscovery discovery = {};
    if (!Esp32BaseFs::listDirInfo(path_, recordStoreDiscoverEntry, &discovery)) {
        setError(StoreError::ReadFailed, StoreState::StructuralFault);
        return false;
    }
    if (discovery.tooMany || discovery.count > MAX_RUNTIME_SEGMENTS) {
        setError(StoreError::TooManySegments, StoreState::StructuralFault);
        return false;
    }
    if (discovery.unknownEntry) {
        setError(StoreError::HeaderInvalid, StoreState::StructuralFault);
        return false;
    }
    for (size_t i = 0; i < discovery.temporaryCount; ++i) {
        char temporaryPath[MAX_STORE_PATH_LENGTH];
        if (recordStoreFormatSegmentPath(path_, discovery.temporaryIds[i], ".tmp",
                                         temporaryPath, sizeof(temporaryPath))) {
            Esp32BaseFs::removeFileWithRecovery(temporaryPath);
        }
    }
    recordStoreSortDiscovered(discovery);
    segmentCount_ = static_cast<uint8_t>(discovery.count);
    currentStoreBytes_ = kControlFileBytes;
    bool invalidHeader = false;
    uint32_t previousEndExclusive = 0;
    bool havePreviousRange = false;
    bool previousRangeExhaustedIds = false;
    uint32_t derivedNextId = firstVisibleId_;
    bool idExhausted = controlNextRecordId_ == 0;
    for (size_t i = 0; i < discovery.count; ++i) {
        SegmentRuntime& segment = segments_[i];
        segment = SegmentRuntime();
        segment.firstRecordId = discovery.segments[i].firstRecordId;
        segment.fileBytes = discovery.segments[i].fileBytes;
        if (UINT32_MAX - currentStoreBytes_ < segment.fileBytes) {
            setError(StoreError::FileSizeMismatch, StoreState::StructuralFault);
            return false;
        }
        currentStoreBytes_ += segment.fileBytes;
        if (!scanOneSegment(segment)) {
            setError(StoreError::ReadFailed, StoreState::StructuralFault);
            return false;
        }
        invalidHeader = invalidHeader || !segment.headerValid;
        if (segment.candidateSlots > 0) {
            if (segment.firstRecordId > UINT32_MAX - (segment.candidateSlots - 1U)) {
                idExhausted = true;
            } else {
                const uint32_t endInclusive = segment.firstRecordId + segment.candidateSlots - 1U;
                if (havePreviousRange &&
                    (previousRangeExhaustedIds || segment.firstRecordId < previousEndExclusive)) {
                    setError(StoreError::SegmentOverlap, StoreState::StructuralFault);
                    return false;
                }
                havePreviousRange = true;
                previousRangeExhaustedIds = endInclusive == UINT32_MAX;
                previousEndExclusive = previousRangeExhaustedIds ? 0 : endInclusive + 1U;
                if (endInclusive >= firstVisibleId_) {
                    newestRecordId_ = endInclusive;
                }
                if (endInclusive == UINT32_MAX) {
                    idExhausted = true;
                } else if (endInclusive + 1U > derivedNextId) {
                    derivedNextId = endInclusive + 1U;
                }
            }
        }
    }
    recalculateRuntimeCounts();
    if (idExhausted) {
        nextRecordId_ = 0;
    } else {
        nextRecordId_ = derivedNextId > controlNextRecordId_ ? derivedNextId : controlNextRecordId_;
        if (nextRecordId_ == 0) nextRecordId_ = firstVisibleId_;
    }
    error_ = nextRecordId_ == 0 ? StoreError::IdExhausted :
             (invalidHeader ? StoreError::HeaderInvalid : StoreError::None);
    state_ = nextRecordId_ == 0 ? StoreState::WriteFault :
             ((damagedRecordCount_ > 0 || invalidHeader) ? StoreState::Degraded : StoreState::Ready);
    bool cleanupFailed = false;
    while (currentStoreBytes_ > maximumStoreBytes_ && segmentCount_ > 0) {
        if (!removeOldestSegment()) {
            setError(StoreError::CleanupFailed, StoreState::WriteFault);
            cleanupFailed = true;
            break;
        }
    }
    if (!cleanupFailed) {
        invalidHeader = false;
        for (uint8_t i = 0; i < segmentCount_; ++i) {
            invalidHeader = invalidHeader || !segments_[i].headerValid;
        }
        error_ = nextRecordId_ == 0 ? StoreError::IdExhausted :
                 (invalidHeader ? StoreError::HeaderInvalid : StoreError::None);
        state_ = nextRecordId_ == 0 ? StoreState::WriteFault :
                 ((damagedRecordCount_ > 0 || invalidHeader) ? StoreState::Degraded : StoreState::Ready);
    }
    return true;
}

void Esp32BaseRecordStore::recalculateRuntimeCounts() {
    recordCount_ = 0;
    damagedRecordCount_ = 0;
    oldestRecordId_ = 0;
    newestRecordId_ = 0;
    for (uint8_t i = 0; i < segmentCount_; ++i) {
        recordCount_ += segments_[i].validCount;
        damagedRecordCount_ += segments_[i].damagedCount;
        if (oldestRecordId_ == 0 && segments_[i].oldestValidId != 0) {
            oldestRecordId_ = segments_[i].oldestValidId;
        }
        if (segments_[i].candidateSlots > 0 &&
            segments_[i].firstRecordId <= UINT32_MAX - (segments_[i].candidateSlots - 1U)) {
            const uint32_t endInclusive = segments_[i].firstRecordId + segments_[i].candidateSlots - 1U;
            if (endInclusive >= firstVisibleId_) newestRecordId_ = endInclusive;
        }
    }
}

bool Esp32BaseRecordStore::captureStartTime(RecordStartTime& startTime) const {
    if (!isReady()) return false;
    const Esp32BaseTime::Snapshot now = Esp32BaseTime::snapshot();
    if (now.bootId == 0) return false;
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

bool Esp32BaseRecordStore::removeOldestSegment() {
    if (segmentCount_ == 0) return false;
    char segmentPath[MAX_STORE_PATH_LENGTH];
    if (!recordStoreFormatSegmentPath(path_, segments_[0].firstRecordId, ".seg",
                                      segmentPath, sizeof(segmentPath))) {
        return false;
    }
    Esp32BaseFs::RemoveFileResult result = Esp32BaseFs::removeFileWithRecovery(segmentPath);
    if (result == Esp32BaseFs::REMOVE_FILE_CLEARED) {
        result = Esp32BaseFs::removeFileWithRecovery(segmentPath);
    }
    if (result != Esp32BaseFs::REMOVE_FILE_DELETED) return false;
    currentStoreBytes_ = currentStoreBytes_ >= segments_[0].fileBytes
        ? currentStoreBytes_ - segments_[0].fileBytes : kControlFileBytes;
    for (uint8_t i = 1; i < segmentCount_; ++i) segments_[i - 1] = segments_[i];
    --segmentCount_;
    segments_[segmentCount_] = SegmentRuntime();
    recalculateRuntimeCounts();
    return true;
}

bool Esp32BaseRecordStore::ensureBudgetForAppend(bool& appendExisting) {
    appendExisting = false;
    if (segmentCount_ > 0) {
        const SegmentRuntime& latest = segments_[segmentCount_ - 1U];
        appendExisting = latest.headerValid && !latest.sealed &&
                         latest.candidateSlots == latest.fullSlots &&
                         latest.firstRecordId <= UINT32_MAX - latest.candidateSlots &&
                         latest.firstRecordId + latest.candidateSlots == nextRecordId_;
    }
    uint32_t required = appendExisting ? slotSizeBytes_ : kSegmentHeaderBytes + slotSizeBytes_;
    while ((static_cast<uint64_t>(currentStoreBytes_) + required > maximumStoreBytes_) ||
           (!appendExisting && segmentCount_ >= MAX_RUNTIME_SEGMENTS)) {
        if (segmentCount_ == 0) return false;
        const bool removingActive = appendExisting && segmentCount_ == 1;
        if (!removeOldestSegment()) return false;
        if (removingActive) appendExisting = false;
        required = appendExisting ? slotSizeBytes_ : kSegmentHeaderBytes + slotSizeBytes_;
    }
    return true;
}

bool Esp32BaseRecordStore::appendToExistingSegment(SegmentRuntime& segment,
                                                    const uint8_t* metadata,
                                                    const uint8_t* payload,
                                                    const uint8_t* crcBytes) {
    char segmentPath[MAX_STORE_PATH_LENGTH];
    if (!recordStoreFormatSegmentPath(path_, segment.firstRecordId, ".seg",
                                      segmentPath, sizeof(segmentPath))) return false;
    const esp32base_internal::FsWriteSegment writes[] = {
        {metadata, kRecordMetadataBytes},
        {payload, payloadSizeBytes_},
        {crcBytes, kRecordCrcBytes}
    };
    const uint32_t offset = segment.fileBytes;
    if (!esp32base_internal::fsAppendSegments(segmentPath, writes, 3)) return false;
    RecordSlotData verify = {};
    if (recordStoreReadRecord(segmentPath, offset, slotSizeBytes_, payloadSizeBytes_,
                              nextRecordId_, nullptr, verify) != RECORD_SLOT_VALID) {
        return false;
    }
    segment.fileBytes += slotSizeBytes_;
    ++segment.candidateSlots;
    ++segment.fullSlots;
    ++segment.validCount;
    if (segment.oldestValidId == 0) segment.oldestValidId = nextRecordId_;
    segment.newestValidId = nextRecordId_;
    segment.sealed = segment.fileBytes > UINT32_MAX - slotSizeBytes_ ||
                     segment.fileBytes + slotSizeBytes_ > segment.fileLimitBytes;
    currentStoreBytes_ += slotSizeBytes_;
    return true;
}

bool Esp32BaseRecordStore::createSegmentWithRecord(const uint8_t* metadata,
                                                    const uint8_t* payload,
                                                    const uint8_t* crcBytes) {
    if (segmentCount_ >= MAX_RUNTIME_SEGMENTS) return false;
    char finalPath[MAX_STORE_PATH_LENGTH];
    char temporaryPath[MAX_STORE_PATH_LENGTH];
    if (!recordStoreFormatSegmentPath(path_, nextRecordId_, ".seg", finalPath, sizeof(finalPath)) ||
        !recordStoreFormatSegmentPath(path_, nextRecordId_, ".tmp", temporaryPath, sizeof(temporaryPath)) ||
        Esp32BaseFs::exists(finalPath)) {
        return false;
    }
    Esp32BaseFs::removeFileWithRecovery(temporaryPath);
    uint8_t header[kSegmentHeaderBytes];
    recordStoreBuildSegmentHeader(header, storeVersion_, payloadSizeBytes_, slotSizeBytes_,
                                  nextRecordId_, segmentFileLimitBytes_);
    const esp32base_internal::FsWriteSegment writes[] = {
        {header, sizeof(header)},
        {metadata, kRecordMetadataBytes},
        {payload, payloadSizeBytes_},
        {crcBytes, kRecordCrcBytes}
    };
    if (!esp32base_internal::fsCreateWithSegments(temporaryPath, writes, 4)) {
        Esp32BaseFs::removeFileWithRecovery(temporaryPath);
        return false;
    }
    uint8_t verifyHeader[kSegmentHeaderBytes];
    size_t readLength = 0;
    uint32_t storedLimit = 0;
    RecordSlotData verifyRecord = {};
    if (!Esp32BaseFs::readBytesAt(temporaryPath, 0, verifyHeader, sizeof(verifyHeader), &readLength) ||
        readLength != sizeof(verifyHeader) ||
        !recordStoreSegmentHeaderMatches(verifyHeader, storeVersion_, payloadSizeBytes_,
                                         slotSizeBytes_, nextRecordId_,
                                         kSegmentHeaderBytes + slotSizeBytes_, storedLimit) ||
        recordStoreReadRecord(temporaryPath, kSegmentHeaderBytes, slotSizeBytes_,
                              payloadSizeBytes_, nextRecordId_, nullptr,
                              verifyRecord) != RECORD_SLOT_VALID) {
        Esp32BaseFs::removeFileWithRecovery(temporaryPath);
        return false;
    }
    if (!Esp32BaseFs::rename(temporaryPath, finalPath)) {
        Esp32BaseFs::removeFileWithRecovery(temporaryPath);
        return false;
    }
    SegmentRuntime& segment = segments_[segmentCount_++];
    segment = SegmentRuntime();
    segment.firstRecordId = nextRecordId_;
    segment.fileBytes = kSegmentHeaderBytes + slotSizeBytes_;
    segment.fileLimitBytes = segmentFileLimitBytes_;
    segment.candidateSlots = 1;
    segment.fullSlots = 1;
    segment.validCount = 1;
    segment.oldestValidId = nextRecordId_;
    segment.newestValidId = nextRecordId_;
    segment.headerValid = true;
    segment.sealed = segment.fileBytes > UINT32_MAX - slotSizeBytes_ ||
                     segment.fileBytes + slotSizeBytes_ > segment.fileLimitBytes;
    currentStoreBytes_ += segment.fileBytes;
    return true;
}

bool Esp32BaseRecordStore::appendWithDuration(uint32_t durationSec,
                                              const uint8_t* payload,
                                              size_t payloadSizeBytes) {
    if (!isWritable() || !payload || payloadSizeBytes != payloadSizeBytes_) {
        if (!payload || payloadSizeBytes != payloadSizeBytes_) error_ = StoreError::InvalidPayload;
        return false;
    }
    if (nextRecordId_ == 0) {
        setError(StoreError::IdExhausted, StoreState::WriteFault);
        return false;
    }
    const Esp32BaseTime::Snapshot now = Esp32BaseTime::snapshot();
    if (now.bootId == 0 || durationSec > now.uptimeSec) {
        error_ = StoreError::InvalidStartTime;
        return false;
    }
    bool appendExisting = false;
    if (!ensureBudgetForAppend(appendExisting)) {
        setError(StoreError::CleanupFailed, StoreState::WriteFault);
        return false;
    }
    const uint32_t required = appendExisting ? slotSizeBytes_ : kSegmentHeaderBytes + slotSizeBytes_;
    if (static_cast<uint64_t>(required) + minimumFileSystemFreeBytes_ > Esp32BaseFs::freeBytes()) {
        setError(StoreError::InsufficientFreeSpace, StoreState::WriteFault);
        return false;
    }
    uint8_t metadata[kRecordMetadataBytes];
    recordStoreWriteU32(metadata, nextRecordId_);
    recordStoreWriteU32(metadata + 4, now.synced ? now.epochSec : 0);
    recordStoreWriteU32(metadata + 8, now.bootId);
    recordStoreWriteU32(metadata + 12, now.uptimeSec);
    recordStoreWriteU32(metadata + 16, durationSec);
    uint32_t crc = recordStoreCrcUpdate(0xFFFFFFFFUL, metadata, sizeof(metadata));
    crc = ~recordStoreCrcUpdate(crc, payload, payloadSizeBytes);
    uint8_t crcBytes[kRecordCrcBytes];
    recordStoreWriteU32(crcBytes, crc);
    const uint32_t recordId = nextRecordId_;
    const bool stored = appendExisting
        ? appendToExistingSegment(segments_[segmentCount_ - 1U], metadata, payload, crcBytes)
        : createSegmentWithRecord(metadata, payload, crcBytes);
    if (!stored) {
        setError(StoreError::WriteFailed, StoreState::WriteFault);
        return false;
    }
    ++recordCount_;
    if (oldestRecordId_ == 0) oldestRecordId_ = recordId;
    newestRecordId_ = recordId;
    nextRecordId_ = recordId == UINT32_MAX ? 0 : recordId + 1U;
    bool invalidSegmentHeader = false;
    for (uint8_t i = 0; i < segmentCount_; ++i) {
        invalidSegmentHeader = invalidSegmentHeader || !segments_[i].headerValid;
    }
    error_ = nextRecordId_ == 0 ? StoreError::IdExhausted :
             (invalidSegmentHeader ? StoreError::HeaderInvalid : StoreError::None);
    state_ = nextRecordId_ == 0 ? StoreState::WriteFault :
             (damagedRecordCount_ > 0 ? StoreState::Degraded : StoreState::Ready);
    return true;
}

bool Esp32BaseRecordStore::readLatest(uint32_t offset,
                                      uint32_t limit,
                                      uint8_t* scratchBuffer,
                                      size_t scratchBufferBytes,
                                      ReadCallback callback,
                                      void* user) {
    if (!isReady() || !scratchBuffer || scratchBufferBytes < payloadSizeBytes_ ||
        !callback || limit == 0) return false;
    LatestReadContext context = {0, firstVisibleId_, offset, limit, 0, 0,
                                 payloadSizeBytes_, scratchBuffer, callback, user, false};
    for (uint8_t index = segmentCount_; index > 0; --index) {
        const SegmentRuntime& segment = segments_[index - 1U];
        if (!segment.headerValid || segment.fullSlots == 0) continue;
        char segmentPath[MAX_STORE_PATH_LENGTH];
        if (!recordStoreFormatSegmentPath(path_, segment.firstRecordId, ".seg",
                                          segmentPath, sizeof(segmentPath))) return false;
        context.firstRecordId = segment.firstRecordId;
        const bool ok = recordStoreVisitSegmentSlots(segmentPath, segment.firstRecordId,
                                                      slotSizeBytes_, payloadSizeBytes_,
                                                      segment.fullSlots, true, scratchBuffer,
                                                      recordStoreReadLatestSlot, &context);
        if (!ok && !context.completed) {
            setError(StoreError::ReadFailed, StoreState::WriteFault);
            return false;
        }
        if (context.completed) break;
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
    if (recordId < firstVisibleId_) return RecordReadResult::NotFound;
    for (uint8_t i = 0; i < segmentCount_; ++i) {
        const SegmentRuntime& segment = segments_[i];
        if (recordId < segment.firstRecordId || segment.candidateSlots == 0) continue;
        const uint32_t slot = recordId - segment.firstRecordId;
        if (slot >= segment.candidateSlots) continue;
        if (!segment.headerValid || slot >= segment.fullSlots) return RecordReadResult::Corrupt;
        char segmentPath[MAX_STORE_PATH_LENGTH];
        if (!recordStoreFormatSegmentPath(path_, segment.firstRecordId, ".seg",
                                          segmentPath, sizeof(segmentPath))) {
            return RecordReadResult::IoError;
        }
        RecordSlotData record = {};
        const RecordSlotState result = recordStoreReadRecord(segmentPath,
                                                              kSegmentHeaderBytes + slot * slotSizeBytes_,
                                                              slotSizeBytes_, payloadSizeBytes_, recordId,
                                                              payloadOut, record);
        if (result == RECORD_SLOT_IO_ERROR) return RecordReadResult::IoError;
        if (result == RECORD_SLOT_CORRUPT) return RecordReadResult::Corrupt;
        recordOut.recordId = record.recordId;
        recordOut.timing = record.timing;
        return RecordReadResult::Found;
    }
    return RecordReadResult::NotFound;
}

bool Esp32BaseRecordStore::writeControlHeader(uint8_t copyIndex,
                                               uint32_t firstVisibleId,
                                               uint32_t nextRecordIdHint,
                                               uint32_t sequence) {
    uint8_t header[kControlHeaderBytes];
    recordStoreBuildControlHeader(header, storeVersion_, payloadSizeBytes_, slotSizeBytes_,
                                  firstVisibleId, nextRecordIdHint, sequence);
    if (!Esp32BaseFs::writeBytesAt(controlPath_, copyIndex * kControlHeaderBytes,
                                   header, sizeof(header))) return false;
    uint8_t verify[kControlHeaderBytes];
    size_t readLength = 0;
    return Esp32BaseFs::readBytesAt(controlPath_, copyIndex * kControlHeaderBytes,
                                    verify, sizeof(verify), &readLength) &&
           readLength == sizeof(verify) &&
           recordStoreControlHeaderMatches(verify, storeVersion_, payloadSizeBytes_, slotSizeBytes_) &&
           recordStoreReadU32(verify + 20) == firstVisibleId &&
           recordStoreReadU32(verify + 24) == sequence &&
           recordStoreReadU32(verify + 28) == nextRecordIdHint;
}

bool Esp32BaseRecordStore::clear() {
    if (!isReady()) return false;
    const uint8_t nextHeader = activeHeader_ == 0 ? 1 : 0;
    const uint32_t clearBoundary = nextRecordId_ ? nextRecordId_ : UINT32_MAX;
    if (!writeControlHeader(nextHeader, clearBoundary, nextRecordId_, headerSequence_ + 1U)) {
        setError(StoreError::WriteFailed, StoreState::WriteFault);
        return false;
    }
    activeHeader_ = nextHeader;
    ++headerSequence_;
    firstVisibleId_ = clearBoundary;
    controlNextRecordId_ = nextRecordId_;
    recordCount_ = 0;
    damagedRecordCount_ = 0;
    oldestRecordId_ = 0;
    newestRecordId_ = 0;
    bool cleanupOk = true;
    while (segmentCount_ > 0) {
        if (!removeOldestSegment()) {
            cleanupOk = false;
            break;
        }
    }
    for (uint8_t i = 0; i < segmentCount_; ++i) {
        segments_[i].validCount = 0;
        segments_[i].damagedCount = 0;
        segments_[i].oldestValidId = 0;
        segments_[i].newestValidId = 0;
    }
    recordCount_ = 0;
    damagedRecordCount_ = 0;
    oldestRecordId_ = 0;
    newestRecordId_ = 0;
    error_ = nextRecordId_ == 0 ? StoreError::IdExhausted :
             (cleanupOk ? StoreError::None : StoreError::CleanupFailed);
    state_ = nextRecordId_ == 0 ? StoreState::WriteFault :
             (cleanupOk ? StoreState::Ready : StoreState::Degraded);
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
    status.segmentCount = segmentCount_;
    status.segmentFileLimitBytes = segmentFileLimitBytes_;
    status.currentStoreBytes = currentStoreBytes_;
    status.maximumStoreBytes = maximumStoreBytes_;
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
    return Esp32BaseTime::resolveCurrentBootEvent(timing.completedBootId,
                                                   timing.completedUptimeSec,
                                                   &epochSec);
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
        case StoreError::TooManySegments: return "too_many_segments";
        case StoreError::SegmentOverlap: return "segment_overlap";
        case StoreError::ReadFailed: return "read_failed";
        case StoreError::WriteFailed: return "write_failed";
        case StoreError::VerifyFailed: return "verify_failed";
        case StoreError::CleanupFailed: return "cleanup_failed";
        case StoreError::InvalidStartTime: return "invalid_start_time";
        case StoreError::InvalidPayload: return "invalid_payload";
        case StoreError::IdExhausted: return "id_exhausted";
        default: return "unknown";
    }
}

#endif
