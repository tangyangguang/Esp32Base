#include <unity.h>

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "runtime/Esp32BaseFs.h"
#include "runtime/Esp32BaseAppEvents.h"
#include "runtime/Esp32BaseRecordStore.h"
#include "runtime/Esp32BaseTime.h"
#include "core/internal/Esp32BaseConfigInternal.h"
#include "runtime/internal/Esp32BaseFsInternal.h"

namespace {

std::map<std::string, std::vector<uint8_t>> g_files;
std::set<std::string> g_directories;
std::set<std::string> g_removeFailurePaths;
size_t g_totalBytes = 1024 * 1024;
Esp32BaseTime::Snapshot g_time = {false, Esp32BaseTime::SOURCE_UPTIME, 0, 10, 1, 0};
uint32_t g_fixedSlotVisitCalls = 0;
uint32_t g_eventStoreWriteAttempts = 0;
uint32_t g_persistedActiveConditionIdBits = 0;
uint32_t g_conditionStateWriteCount = 0;
bool g_conditionStateExists = false;
bool g_conditionStateReadFails = false;
bool g_conditionStateWriteFails = false;
bool g_fileSystemWriteFails = false;

void resetHarness() {
    g_files.clear();
    g_directories.clear();
    g_directories.insert("/");
    g_removeFailurePaths.clear();
    g_totalBytes = 1024 * 1024;
    g_time = {false, Esp32BaseTime::SOURCE_UPTIME, 0, 10, 1, 0};
    g_fixedSlotVisitCalls = 0;
    g_eventStoreWriteAttempts = 0;
    g_persistedActiveConditionIdBits = 0;
    g_conditionStateWriteCount = 0;
    g_conditionStateExists = false;
    g_conditionStateReadFails = false;
    g_conditionStateWriteFails = false;
    g_fileSystemWriteFails = false;
    nativeMillisValue() = 0;
}

Esp32BaseRecordStore::StoreDefinition definition(const char* name = "watering",
                                                  uint16_t version = 1,
                                                  uint32_t maximumBytes = 224,
                                                  uint32_t minimumFreeBytes = 0) {
    Esp32BaseRecordStore::StoreDefinition value;
    value.recordTypeName = name;
    value.storeVersion = version;
    value.payloadSizeBytes = 8;
    value.maximumStoreBytes = maximumBytes;
    value.minimumFileSystemFreeBytes = minimumFreeBytes;
    return value;
}

void payload(uint8_t* out, uint32_t value) {
    for (size_t i = 0; i < 8; ++i) out[i] = static_cast<uint8_t>(value + i);
}

struct IdCollector {
    std::vector<uint32_t> ids;
};

void collectId(const Esp32BaseRecordStore::RecordView& record, void* user) {
    static_cast<IdCollector*>(user)->ids.push_back(record.recordId);
}

} // namespace

bool Esp32BaseFs::begin() { return true; }
bool Esp32BaseFs::isReady() { return true; }
bool Esp32BaseFs::format() { g_files.clear(); return true; }
bool Esp32BaseFs::createFixedFile(const char* path, uint32_t size, uint8_t fillByte) {
    g_files[path] = std::vector<uint8_t>(size, fillByte);
    return true;
}
bool Esp32BaseFs::writeBytes(const char* path, const uint8_t* data, size_t length) {
    if (!path || (!data && length > 0)) return false;
    g_files[path] = std::vector<uint8_t>(data, data + length);
    return true;
}
bool Esp32BaseFs::readBytes(const char* path, uint8_t* out, size_t maximum, size_t* readLength) {
    return readBytesAt(path, 0, out, maximum, readLength);
}
bool Esp32BaseFs::appendBytes(const char* path, const uint8_t* data, size_t length) {
    auto found = g_files.find(path ? path : "");
    if (found == g_files.end() || (!data && length > 0)) return false;
    found->second.insert(found->second.end(), data, data + length);
    return true;
}
bool Esp32BaseFs::readBytesAt(const char* path, uint32_t offset, uint8_t* out, size_t maximum, size_t* readLength) {
    if (readLength) *readLength = 0;
    auto found = g_files.find(path ? path : "");
    if (found == g_files.end() || !out || offset > found->second.size()) return false;
    const size_t length = std::min(maximum, found->second.size() - offset);
    std::copy(found->second.begin() + offset, found->second.begin() + offset + length, out);
    if (readLength) *readLength = length;
    return true;
}
bool Esp32BaseFs::writeBytesAt(const char* path, uint32_t offset, const uint8_t* data, size_t length) {
    if (g_fileSystemWriteFails) return false;
    auto found = g_files.find(path ? path : "");
    if (found == g_files.end() || (!data && length > 0) || offset > found->second.size() || length > found->second.size() - offset) return false;
    std::copy(data, data + length, found->second.begin() + offset);
    return true;
}
Esp32BaseFs::RemoveFileResult Esp32BaseFs::removeFileWithRecovery(const char* path) {
    if (g_removeFailurePaths.count(path ? path : "")) return REMOVE_FILE_FAILED;
    return g_files.erase(path ? path : "") ? REMOVE_FILE_DELETED : REMOVE_FILE_FAILED;
}
bool Esp32BaseFs::removeFile(const char* path) { return g_files.erase(path ? path : "") != 0; }
bool Esp32BaseFs::rename(const char* from, const char* to) {
    auto found = g_files.find(from ? from : "");
    if (found == g_files.end() || g_files.count(to ? to : "")) return false;
    g_files[to] = found->second;
    g_files.erase(found);
    return true;
}
bool Esp32BaseFs::exists(const char* path) {
    const std::string value = path ? path : "";
    return g_files.count(value) || g_directories.count(value);
}
int64_t Esp32BaseFs::fileSize(const char* path) {
    auto found = g_files.find(path ? path : "");
    return found == g_files.end() ? -1 : static_cast<int64_t>(found->second.size());
}
bool Esp32BaseFs::mkdir(const char* path) {
    const std::string value = path ? path : "";
    const size_t slash = value.find_last_of('/');
    const std::string parent = slash == 0 ? "/" : value.substr(0, slash);
    if (value.empty() || !g_directories.count(parent)) return false;
    g_directories.insert(value);
    return true;
}
bool Esp32BaseFs::rmdir(const char* path) { return g_directories.erase(path ? path : "") != 0; }
bool Esp32BaseFs::listDirInfo(const char* path, ListInfoCallback callback, void* user) {
    const std::string root = path ? path : "";
    if (!callback || !g_directories.count(root)) return false;
    const std::string prefix = root == "/" ? "/" : root + "/";
    for (const auto& entry : g_files) {
        if (entry.first.compare(0, prefix.size(), prefix) != 0) continue;
        const std::string remainder = entry.first.substr(prefix.size());
        if (remainder.empty() || remainder.find('/') != std::string::npos) continue;
        EntryInfo info = {entry.first.c_str(), entry.second.size(), false, 0};
        callback(info, user);
    }
    for (const std::string& directory : g_directories) {
        if (directory == root || directory.compare(0, prefix.size(), prefix) != 0) continue;
        const std::string remainder = directory.substr(prefix.size());
        if (remainder.empty() || remainder.find('/') != std::string::npos) continue;
        EntryInfo info = {directory.c_str(), 0, true, 0};
        callback(info, user);
    }
    return true;
}
bool Esp32BaseFs::listDir(const char* path, ListCallback callback, void* user) {
    struct Frame { ListCallback callback; void* user; } frame = {callback, user};
    return listDirInfo(path, [](const EntryInfo& entry, void* value) {
        Frame* frame = static_cast<Frame*>(value);
        frame->callback(entry.name, entry.size, entry.isDir, frame->user);
    }, &frame);
}
size_t Esp32BaseFs::totalBytes() { return g_totalBytes; }
size_t Esp32BaseFs::usedBytes() {
    size_t used = 0;
    for (const auto& entry : g_files) used += entry.second.size();
    return used;
}
size_t Esp32BaseFs::freeBytes() { return usedBytes() < g_totalBytes ? g_totalBytes - usedBytes() : 0; }
bool Esp32BaseFs::storageInfo(size_t& total, size_t& used) { total = totalBytes(); used = usedBytes(); return true; }

bool esp32base_internal::fsWriteSegmentsAt(const char* path,
                                           uint32_t offset,
                                           const FsWriteSegment* segments,
                                           size_t segmentCount) {
    ++g_eventStoreWriteAttempts;
    if (g_fileSystemWriteFails) return false;
    for (size_t i = 0; i < segmentCount; ++i) {
        if (!Esp32BaseFs::writeBytesAt(path, offset, segments[i].data, segments[i].length)) return false;
        offset += static_cast<uint32_t>(segments[i].length);
    }
    return true;
}

bool esp32base_internal::fsCreateWithSegments(const char* path,
                                               const FsWriteSegment* segments,
                                               size_t segmentCount) {
    ++g_eventStoreWriteAttempts;
    if (g_fileSystemWriteFails || Esp32BaseFs::exists(path)) return false;
    std::vector<uint8_t> bytes;
    for (size_t i = 0; i < segmentCount; ++i) {
        bytes.insert(bytes.end(), segments[i].data, segments[i].data + segments[i].length);
    }
    g_files[path ? path : ""] = bytes;
    return true;
}

bool esp32base_internal::fsAppendSegments(const char* path,
                                           const FsWriteSegment* segments,
                                           size_t segmentCount) {
    ++g_eventStoreWriteAttempts;
    if (g_fileSystemWriteFails) return false;
    auto found = g_files.find(path ? path : "");
    if (found == g_files.end()) return false;
    for (size_t i = 0; i < segmentCount; ++i) {
        found->second.insert(found->second.end(), segments[i].data,
                             segments[i].data + segments[i].length);
    }
    return true;
}

bool esp32base_internal::fsVisitBytesAt(const char* path,
                                        uint32_t offset,
                                        uint32_t length,
                                        FsReadVisitor visitor,
                                        void* user) {
    std::vector<uint8_t> buffer(length);
    size_t readLength = 0;
    return Esp32BaseFs::readBytesAt(path, offset, buffer.data(), buffer.size(), &readLength) &&
           readLength == length && visitor(buffer.data(), buffer.size(), user);
}

bool esp32base_internal::fsVisitFixedSlots(const char* path,
                                           uint32_t dataOffset,
                                           uint32_t slotSize,
                                           uint32_t capacity,
                                           uint32_t firstSlot,
                                           uint32_t slotCount,
                                           bool descending,
                                           FsFixedSlotVisitor visitor,
                                           void* user) {
    ++g_fixedSlotVisitCalls;
    auto found = g_files.find(path ? path : "");
    if (found == g_files.end() || slotSize == 0 || capacity == 0 || firstSlot >= capacity ||
        slotCount > capacity || !visitor ||
        static_cast<uint64_t>(dataOffset) + static_cast<uint64_t>(slotSize) * capacity > found->second.size()) {
        return false;
    }
    uint32_t slot = firstSlot;
    for (uint32_t visited = 0; visited < slotCount; ++visited) {
        const uint32_t offset = dataOffset + slot * slotSize;
        if (!visitor(slot, 0, found->second.data() + offset, slotSize, user)) return false;
        if (descending) slot = slot == 0 ? capacity - 1U : slot - 1U;
        else slot = slot + 1U == capacity ? 0 : slot + 1U;
    }
    return true;
}

Esp32BaseTime::Snapshot Esp32BaseTime::snapshot() { return g_time; }
bool Esp32BaseTime::resolveCurrentBootEvent(uint32_t bootId, uint32_t uptimeSec, uint32_t* epochSec) {
    if (!epochSec || !g_time.synced || bootId != g_time.bootId || g_time.bootStartEpochSec == 0) return false;
    *epochSec = g_time.bootStartEpochSec + uptimeSec;
    return true;
}

esp32base_internal::ConfigUInt32ReadResult esp32base_internal::readConfigUInt32(
    const char*, const char*, uint32_t& value) {
    if (g_conditionStateReadFails) return ConfigUInt32ReadResult::Error;
    if (!g_conditionStateExists) {
        value = 0;
        return ConfigUInt32ReadResult::NotFound;
    }
    value = g_persistedActiveConditionIdBits;
    return ConfigUInt32ReadResult::Found;
}

bool esp32base_internal::writeConfigUInt32(const char*, const char*, uint32_t value) {
    ++g_conditionStateWriteCount;
    if (g_conditionStateWriteFails) return false;
    g_persistedActiveConditionIdBits = value;
    g_conditionStateExists = true;
    return true;
}

#include "runtime/Esp32BaseRecordStore.inc"
#include "runtime/Esp32BaseAppEvents.inc"

void setUp() { resetHarness(); }
void tearDown() {}

std::string controlPath(const Esp32BaseRecordStore& store) {
    return std::string(store.path()) + "/control.bin";
}

std::string segmentPath(const Esp32BaseRecordStore& store, uint32_t firstId = 1) {
    char path[128];
    std::snprintf(path, sizeof(path), "%s/%08lx.seg", store.path(),
                  static_cast<unsigned long>(firstId));
    return path;
}

void appendRecords(Esp32BaseRecordStore& store, uint32_t first, uint32_t last) {
    uint8_t bytes[8];
    for (uint32_t id = first; id <= last; ++id) {
        payload(bytes, id);
        ++g_time.uptimeSec;
        TEST_ASSERT_TRUE(store.appendInstant(bytes, sizeof(bytes)));
    }
}

void test_create_calculates_capacity_and_store_budget() {
    Esp32BaseRecordStore store;
    TEST_ASSERT_TRUE(store.begin(definition()));
    Esp32BaseRecordStore::StoreStatus status;
    TEST_ASSERT_TRUE(store.readStatus(status));
    TEST_ASSERT_EQUAL_UINT32(2, status.capacity);
    TEST_ASSERT_EQUAL_UINT32(32, status.slotSizeBytes);
    TEST_ASSERT_EQUAL_UINT32(4096, status.segmentFileLimitBytes);
    TEST_ASSERT_EQUAL_UINT32(0, status.segmentCount);
    TEST_ASSERT_EQUAL_UINT32(128, status.currentStoreBytes);
    TEST_ASSERT_EQUAL_UINT32(224, status.maximumStoreBytes);
    TEST_ASSERT_EQUAL_STRING("/esp32base/records/watering.v1", status.path);
    TEST_ASSERT_TRUE(Esp32BaseFs::exists(controlPath(store).c_str()));
    TEST_ASSERT_EQUAL_UINT32(0, g_fixedSlotVisitCalls);
}

void test_segment_rotation_reads_latest_first() {
    Esp32BaseRecordStore store;
    TEST_ASSERT_TRUE(store.begin(definition("watering", 1, 4320)));
    appendRecords(store, 1, 132);
    IdCollector collected;
    uint8_t scratch[8];
    TEST_ASSERT_TRUE(store.readLatest(0, 10, scratch, sizeof(scratch), collectId, &collected));
    TEST_ASSERT_EQUAL_UINT32(5, collected.ids.size());
    TEST_ASSERT_EQUAL_UINT32(132, collected.ids[0]);
    TEST_ASSERT_EQUAL_UINT32(131, collected.ids[1]);
    TEST_ASSERT_EQUAL_UINT32(130, collected.ids[2]);
    Esp32BaseRecordStore::StoreStatus status;
    TEST_ASSERT_TRUE(store.readStatus(status));
    TEST_ASSERT_EQUAL_UINT32(5, status.recordCount);
    TEST_ASSERT_TRUE(status.currentStoreBytes <= status.maximumStoreBytes);
    TEST_ASSERT_EQUAL_UINT32(1, status.segmentCount);
    TEST_ASSERT_FALSE(Esp32BaseFs::exists(segmentPath(store, 1).c_str()));
    TEST_ASSERT_TRUE(Esp32BaseFs::exists(segmentPath(store, 128).c_str()));
}

void test_smaller_budget_removes_oversized_old_segment_and_resets_visible_range() {
    Esp32BaseRecordStore original;
    TEST_ASSERT_TRUE(original.begin(definition("watering", 1, 4320)));
    appendRecords(original, 1, 132);

    Esp32BaseRecordStore reopened;
    TEST_ASSERT_TRUE(reopened.begin(definition("watering", 1, 224)));
    Esp32BaseRecordStore::StoreStatus status;
    TEST_ASSERT_TRUE(reopened.readStatus(status));
    TEST_ASSERT_EQUAL_UINT32(0, status.recordCount);
    TEST_ASSERT_EQUAL_UINT32(0, status.oldestRecordId);
    TEST_ASSERT_EQUAL_UINT32(0, status.newestRecordId);
    TEST_ASSERT_EQUAL_UINT32(133, status.nextRecordId);
    TEST_ASSERT_EQUAL_UINT32(128, status.currentStoreBytes);
}

void test_completed_record_uses_duration_and_resolves_time() {
    Esp32BaseRecordStore store;
    TEST_ASSERT_TRUE(store.begin(definition()));
    Esp32BaseRecordStore::RecordStartTime start;
    TEST_ASSERT_TRUE(store.captureStartTime(start));
    g_time.uptimeSec = 40;
    g_time.synced = true;
    g_time.epochSec = 1700000040UL;
    g_time.bootStartEpochSec = 1700000000UL;
    uint8_t bytes[8]; payload(bytes, 1);
    TEST_ASSERT_TRUE(store.appendCompleted(start, bytes, sizeof(bytes)));
    Esp32BaseRecordStore::RecordMetadata record;
    uint8_t output[8];
    TEST_ASSERT_EQUAL(Esp32BaseRecordStore::RecordReadResult::Found, store.readById(1, output, sizeof(output), record));
    TEST_ASSERT_EQUAL_UINT32(30, record.timing.durationSec);
    uint32_t epoch = 0;
    TEST_ASSERT_TRUE(Esp32BaseRecordStore::resolveStartedEpoch(record.timing, epoch));
    TEST_ASSERT_EQUAL_UINT32(1700000010UL, epoch);
}

void test_corrupt_record_is_skipped_and_reported() {
    Esp32BaseRecordStore store;
    TEST_ASSERT_TRUE(store.begin(definition()));
    uint8_t bytes[8]; payload(bytes, 1);
    TEST_ASSERT_TRUE(store.appendInstant(bytes, sizeof(bytes)));
    g_files[segmentPath(store)][32 + 20] ^= 0x55;
    TEST_ASSERT_TRUE(store.reload());
    Esp32BaseRecordStore::StoreStatus status;
    store.readStatus(status);
    TEST_ASSERT_EQUAL(Esp32BaseRecordStore::StoreState::Degraded, status.state);
    TEST_ASSERT_EQUAL_UINT32(1, status.damagedRecordCount);
    TEST_ASSERT_EQUAL_UINT32(0, status.recordCount);
}

void test_corrupt_record_id_is_reported_and_read_as_corrupt() {
    Esp32BaseRecordStore store;
    TEST_ASSERT_TRUE(store.begin(definition()));
    uint8_t bytes[8]; payload(bytes, 1);
    TEST_ASSERT_TRUE(store.appendInstant(bytes, sizeof(bytes)));
    TEST_ASSERT_TRUE(store.appendInstant(bytes, sizeof(bytes)));
    g_files[segmentPath(store)][32] ^= 0x55;
    TEST_ASSERT_TRUE(store.reload());
    Esp32BaseRecordStore::StoreStatus status;
    store.readStatus(status);
    TEST_ASSERT_EQUAL(Esp32BaseRecordStore::StoreState::Degraded, status.state);
    TEST_ASSERT_EQUAL_UINT32(1, status.damagedRecordCount);
    TEST_ASSERT_EQUAL_UINT32(1, status.recordCount);
    Esp32BaseRecordStore::RecordMetadata record;
    uint8_t output[8];
    TEST_ASSERT_EQUAL(Esp32BaseRecordStore::RecordReadResult::Corrupt,
                      store.readById(1, output, sizeof(output), record));
}

void test_torn_tail_is_reserved_and_never_returned() {
    Esp32BaseRecordStore store;
    TEST_ASSERT_TRUE(store.begin(definition("feeding", 1, 4320)));
    appendRecords(store, 1, 1);
    std::vector<uint8_t>& segment = g_files[segmentPath(store)];
    const uint8_t partial[] = {2, 0, 0, 0, 0xAA, 0xBB, 0xCC};
    segment.insert(segment.end(), partial, partial + sizeof(partial));
    TEST_ASSERT_TRUE(store.reload());
    Esp32BaseRecordStore::StoreStatus status;
    TEST_ASSERT_TRUE(store.readStatus(status));
    TEST_ASSERT_EQUAL(Esp32BaseRecordStore::StoreState::Degraded, status.state);
    TEST_ASSERT_EQUAL_UINT32(1, status.damagedRecordCount);
    TEST_ASSERT_EQUAL_UINT32(3, status.nextRecordId);
    Esp32BaseRecordStore::RecordMetadata record;
    uint8_t output[8];
    TEST_ASSERT_EQUAL(Esp32BaseRecordStore::RecordReadResult::Corrupt,
                      store.readById(2, output, sizeof(output), record));
    uint8_t bytes[8]; payload(bytes, 3);
    ++g_time.uptimeSec;
    TEST_ASSERT_TRUE(store.appendInstant(bytes, sizeof(bytes)));
    TEST_ASSERT_TRUE(Esp32BaseFs::exists(segmentPath(store, 3).c_str()));
    TEST_ASSERT_EQUAL(Esp32BaseRecordStore::RecordReadResult::Found,
                      store.readById(3, output, sizeof(output), record));
}

void test_corrupt_segment_header_is_skipped_but_store_remains_writable() {
    Esp32BaseRecordStore store;
    TEST_ASSERT_TRUE(store.begin(definition("feeding", 1, 4320)));
    appendRecords(store, 1, 1);
    g_files[segmentPath(store)][0] ^= 0x5A;
    TEST_ASSERT_TRUE(store.reload());
    Esp32BaseRecordStore::StoreStatus status;
    store.readStatus(status);
    TEST_ASSERT_EQUAL(Esp32BaseRecordStore::StoreState::Degraded, status.state);
    TEST_ASSERT_EQUAL(Esp32BaseRecordStore::StoreError::HeaderInvalid, status.error);
    TEST_ASSERT_EQUAL_UINT32(1, status.damagedRecordCount);
    Esp32BaseRecordStore::RecordMetadata record;
    uint8_t output[8];
    TEST_ASSERT_EQUAL(Esp32BaseRecordStore::RecordReadResult::Corrupt,
                      store.readById(1, output, sizeof(output), record));
    uint8_t bytes[8]; payload(bytes, 2);
    TEST_ASSERT_FALSE(store.appendInstant(bytes, sizeof(bytes) - 1));
    TEST_ASSERT_EQUAL(Esp32BaseRecordStore::StoreError::InvalidPayload, store.lastError());
    ++g_time.uptimeSec;
    TEST_ASSERT_TRUE(store.appendInstant(bytes, sizeof(bytes)));
    TEST_ASSERT_EQUAL(Esp32BaseRecordStore::StoreError::HeaderInvalid, store.lastError());
    TEST_ASSERT_EQUAL(Esp32BaseRecordStore::RecordReadResult::Found,
                      store.readById(2, output, sizeof(output), record));
}

void test_tiny_corrupt_segment_reserves_its_filename_id() {
    Esp32BaseRecordStore store;
    TEST_ASSERT_TRUE(store.begin(definition("feeding", 1, 4320)));
    appendRecords(store, 1, 1);
    g_files[segmentPath(store, 2)] = std::vector<uint8_t>();
    TEST_ASSERT_TRUE(store.reload());
    Esp32BaseRecordStore::StoreStatus status;
    TEST_ASSERT_TRUE(store.readStatus(status));
    TEST_ASSERT_EQUAL(Esp32BaseRecordStore::StoreState::Degraded, status.state);
    TEST_ASSERT_EQUAL(Esp32BaseRecordStore::StoreError::HeaderInvalid, status.error);
    TEST_ASSERT_EQUAL_UINT32(1, status.damagedRecordCount);
    TEST_ASSERT_EQUAL_UINT32(3, status.nextRecordId);
}

void test_overlapping_segment_ranges_are_a_structural_fault() {
    Esp32BaseRecordStore store;
    TEST_ASSERT_TRUE(store.begin(definition("feeding", 1, 8192)));
    appendRecords(store, 1, 2);
    g_files[segmentPath(store, 2)] = g_files[segmentPath(store, 1)];
    TEST_ASSERT_FALSE(store.reload());
    TEST_ASSERT_EQUAL(Esp32BaseRecordStore::StoreState::StructuralFault, store.state());
    TEST_ASSERT_EQUAL(Esp32BaseRecordStore::StoreError::SegmentOverlap, store.lastError());
}

void test_clear_hides_records_and_continues_ids() {
    Esp32BaseRecordStore store;
    TEST_ASSERT_TRUE(store.begin(definition()));
    uint8_t bytes[8]; payload(bytes, 1);
    TEST_ASSERT_TRUE(store.appendInstant(bytes, sizeof(bytes)));
    TEST_ASSERT_TRUE(store.appendInstant(bytes, sizeof(bytes)));
    TEST_ASSERT_TRUE(store.clear());
    TEST_ASSERT_TRUE(store.appendInstant(bytes, sizeof(bytes)));
    Esp32BaseRecordStore::RecordMetadata record;
    uint8_t output[8];
    TEST_ASSERT_EQUAL(Esp32BaseRecordStore::RecordReadResult::NotFound, store.readById(2, output, sizeof(output), record));
    TEST_ASSERT_EQUAL(Esp32BaseRecordStore::RecordReadResult::Found, store.readById(3, output, sizeof(output), record));
}

void test_different_versions_use_different_directories() {
    Esp32BaseRecordStore first;
    Esp32BaseRecordStore second;
    TEST_ASSERT_TRUE(first.begin(definition("feeding", 1)));
    TEST_ASSERT_TRUE(second.begin(definition("feeding", 2)));
    TEST_ASSERT_NOT_EQUAL(0, std::strcmp(first.path(), second.path()));
    TEST_ASSERT_TRUE(Esp32BaseFs::exists(first.path()));
    TEST_ASSERT_TRUE(Esp32BaseFs::exists(second.path()));
}

void test_clearing_current_version_does_not_modify_historical_version() {
    Esp32BaseRecordStore historical;
    Esp32BaseRecordStore current;
    TEST_ASSERT_TRUE(historical.begin(definition("watering", 1)));
    TEST_ASSERT_TRUE(current.begin(definition("watering", 2)));
    uint8_t bytes[8];
    payload(bytes, 1);
    TEST_ASSERT_TRUE(historical.appendInstant(bytes, sizeof(bytes)));
    payload(bytes, 2);
    TEST_ASSERT_TRUE(current.appendInstant(bytes, sizeof(bytes)));

    TEST_ASSERT_TRUE(current.clear());
    Esp32BaseRecordStore::StoreStatus historicalStatus;
    Esp32BaseRecordStore::StoreStatus currentStatus;
    TEST_ASSERT_TRUE(historical.readStatus(historicalStatus));
    TEST_ASSERT_TRUE(current.readStatus(currentStatus));
    TEST_ASSERT_EQUAL_UINT32(1, historicalStatus.recordCount);
    TEST_ASSERT_EQUAL_UINT32(0, currentStatus.recordCount);
    TEST_ASSERT_EQUAL_UINT32(2, currentStatus.nextRecordId);

    Esp32BaseRecordStore::RecordMetadata record;
    uint8_t output[8];
    TEST_ASSERT_EQUAL(Esp32BaseRecordStore::RecordReadResult::Found,
                      historical.readById(1, output, sizeof(output), record));
}

void test_existing_control_wrong_size_is_not_rewritten() {
    Esp32BaseRecordStore store;
    TEST_ASSERT_TRUE(store.begin(definition()));
    g_files[controlPath(store)].resize(100);
    TEST_ASSERT_FALSE(store.reload());
    TEST_ASSERT_EQUAL(Esp32BaseRecordStore::StoreState::StructuralFault, store.state());
    TEST_ASSERT_EQUAL_UINT32(100, g_files[controlPath(store)].size());
}

void test_existing_definition_mismatch_is_not_rewritten() {
    Esp32BaseRecordStore store;
    TEST_ASSERT_TRUE(store.begin(definition()));
    std::vector<uint8_t>& file = g_files[controlPath(store)];
    for (uint32_t copy = 0; copy < 2; ++copy) {
        uint8_t* header = file.data() + copy * 64U;
        recordStoreWriteU16(header + 8, 2);
        recordStoreWriteU32(header + 32, 0);
        recordStoreWriteU32(header + 32, recordStoreCrc(header, 64));
    }
    const std::vector<uint8_t> beforeReload = file;
    TEST_ASSERT_FALSE(store.reload());
    TEST_ASSERT_EQUAL(Esp32BaseRecordStore::StoreError::DefinitionMismatch, store.lastError());
    TEST_ASSERT_EQUAL_UINT32(beforeReload.size(), g_files[controlPath(store)].size());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(beforeReload.data(), g_files[controlPath(store)].data(), beforeReload.size());
}

void test_minimum_free_space_blocks_business_store_creation() {
    g_totalBytes = 200;
    Esp32BaseRecordStore store;
    TEST_ASSERT_FALSE(store.begin(definition("door-opening", 1, 224, 100)));
    TEST_ASSERT_EQUAL(Esp32BaseRecordStore::StoreError::InsufficientFreeSpace, store.lastError());
    TEST_ASSERT_FALSE(Esp32BaseFs::exists("/esp32base/records/door-opening.v1/control.bin"));
}

void test_latest_pagination_crosses_segments() {
    Esp32BaseRecordStore store;
    TEST_ASSERT_TRUE(store.begin(definition("feeding", 1, 4320)));
    appendRecords(store, 1, 129);
    IdCollector collected;
    uint8_t scratch[8];
    TEST_ASSERT_TRUE(store.readLatest(1, 4, scratch, sizeof(scratch), collectId, &collected));
    TEST_ASSERT_EQUAL_UINT32(4, collected.ids.size());
    TEST_ASSERT_EQUAL_UINT32(128, collected.ids[0]);
    TEST_ASSERT_EQUAL_UINT32(127, collected.ids[1]);
    TEST_ASSERT_EQUAL_UINT32(126, collected.ids[2]);
    TEST_ASSERT_EQUAL_UINT32(125, collected.ids[3]);
}

void test_torn_clear_header_falls_back_to_pre_clear_state() {
    Esp32BaseRecordStore store;
    TEST_ASSERT_TRUE(store.begin(definition()));
    uint8_t bytes[8]; payload(bytes, 1);
    TEST_ASSERT_TRUE(store.appendInstant(bytes, sizeof(bytes)));
    const std::vector<uint8_t> oldSegment = g_files[segmentPath(store)];
    TEST_ASSERT_TRUE(store.clear());
    g_files[segmentPath(store)] = oldSegment;
    g_files[controlPath(store)][64] ^= 0x5A;
    TEST_ASSERT_TRUE(store.reload());
    Esp32BaseRecordStore::RecordMetadata record;
    uint8_t output[8];
    TEST_ASSERT_EQUAL(Esp32BaseRecordStore::RecordReadResult::Found,
                      store.readById(1, output, sizeof(output), record));
}

void test_clear_boundary_stays_effective_when_segment_cleanup_fails() {
    Esp32BaseRecordStore store;
    TEST_ASSERT_TRUE(store.begin(definition("watering", 1, 4320)));
    appendRecords(store, 1, 2);
    g_removeFailurePaths.insert(segmentPath(store));
    TEST_ASSERT_TRUE(store.clear());
    Esp32BaseRecordStore::StoreStatus status;
    TEST_ASSERT_TRUE(store.readStatus(status));
    TEST_ASSERT_EQUAL(Esp32BaseRecordStore::StoreState::Degraded, status.state);
    TEST_ASSERT_EQUAL(Esp32BaseRecordStore::StoreError::CleanupFailed, status.error);
    TEST_ASSERT_EQUAL_UINT32(0, status.recordCount);
    Esp32BaseRecordStore::RecordMetadata record;
    uint8_t output[8];
    TEST_ASSERT_EQUAL(Esp32BaseRecordStore::RecordReadResult::NotFound,
                      store.readById(1, output, sizeof(output), record));
    g_removeFailurePaths.clear();
    ++g_time.uptimeSec;
    uint8_t bytes[8]; payload(bytes, 3);
    TEST_ASSERT_TRUE(store.appendInstant(bytes, sizeof(bytes)));
    TEST_ASSERT_TRUE(store.readStatus(status));
    TEST_ASSERT_EQUAL_UINT32(1, status.recordCount);
    TEST_ASSERT_EQUAL(Esp32BaseRecordStore::RecordReadResult::Found,
                      store.readById(3, output, sizeof(output), record));
    TEST_ASSERT_TRUE(store.reload());
    TEST_ASSERT_TRUE(store.readStatus(status));
    TEST_ASSERT_EQUAL_UINT32(1, status.recordCount);
    TEST_ASSERT_EQUAL(Esp32BaseRecordStore::RecordReadResult::NotFound,
                      store.readById(2, output, sizeof(output), record));
}

struct EventCollector {
    std::vector<Esp32BaseAppEvents::EventRecord> events;
};

void collectEvent(const Esp32BaseAppEvents::EventRecord& event, void* user) {
    static_cast<EventCollector*>(user)->events.push_back(event);
}

void test_app_events_uses_compact_payload_and_default_budget() {
    TEST_ASSERT_EQUAL_UINT32(24, sizeof(Esp32BaseAppEvents::EventInput));
    TEST_ASSERT_EQUAL_UINT32(20, sizeof(Esp32BaseAppEvents::ConditionStateTracker));
    TEST_ASSERT_TRUE(Esp32BaseAppEvents::begin());
    TEST_ASSERT_EQUAL_UINT32(2113, Esp32BaseAppEvents::eventCapacity());
    TEST_ASSERT_EQUAL_STRING("/esp32base/records/app-events.v2", Esp32BaseAppEvents::eventStorePath());
    EventCollector empty;
    TEST_ASSERT_TRUE(Esp32BaseAppEvents::readLatest(0, 10, collectEvent, &empty));
    TEST_ASSERT_EQUAL_UINT32(0, empty.events.size());

    Esp32BaseAppEvents::EventInput input;
    input.eventCode = 1002;
    input.reasonCode = 2101;
    input.objectId = 1;
    input.value1 = 1450;
    input.value2 = 2;
    input.flags = 0x0001;
    input.level = Esp32BaseAppEvents::Level::Warning;
    TEST_ASSERT_EQUAL(Esp32BaseAppEvents::DiscreteEventAppendResult::Stored,
                      Esp32BaseAppEvents::appendDiscreteEvent(input));

    EventCollector collected;
    TEST_ASSERT_TRUE(Esp32BaseAppEvents::readLatest(0, 10, collectEvent, &collected));
    TEST_ASSERT_EQUAL_UINT32(1, collected.events.size());
    TEST_ASSERT_EQUAL_UINT32(1, collected.events[0].recordId);
    TEST_ASSERT_EQUAL_UINT32(1002, collected.events[0].eventCode);
    TEST_ASSERT_EQUAL_UINT32(2101, collected.events[0].reasonCode);
    TEST_ASSERT_EQUAL_INT32(1450, collected.events[0].value1);
    TEST_ASSERT_EQUAL_UINT8(0x01, collected.events[0].flags);
    TEST_ASSERT_EQUAL(Esp32BaseAppEvents::EventKind::Discrete, collected.events[0].eventKind);
    TEST_ASSERT_EQUAL_UINT8(0, collected.events[0].conditionId);
    Esp32BaseAppEvents::AppEventsStatus status;
    TEST_ASSERT_TRUE(Esp32BaseAppEvents::readStatus(status));
    TEST_ASSERT_EQUAL_UINT32(48, status.eventStore.slotSizeBytes);
    TEST_ASSERT_EQUAL_UINT32(208, status.eventStore.currentStoreBytes);
}

Esp32BaseAppEvents::EventInput conditionEvent(uint32_t eventCode, uint8_t flags = 0) {
    Esp32BaseAppEvents::EventInput event;
    event.eventCode = eventCode;
    event.flags = flags;
    event.level = Esp32BaseAppEvents::Level::Warning;
    return event;
}

void test_discrete_events_with_identical_values_are_each_stored() {
    TEST_ASSERT_TRUE(Esp32BaseAppEvents::begin());
    const Esp32BaseAppEvents::EventInput event = conditionEvent(4001);
    TEST_ASSERT_EQUAL(Esp32BaseAppEvents::DiscreteEventAppendResult::Stored,
                      Esp32BaseAppEvents::appendDiscreteEvent(event));
    TEST_ASSERT_EQUAL(Esp32BaseAppEvents::DiscreteEventAppendResult::Stored,
                      Esp32BaseAppEvents::appendDiscreteEvent(event));
    TEST_ASSERT_EQUAL_UINT32(2, Esp32BaseAppEvents::eventCount());
}

void test_condition_activation_recovery_and_reactivation_are_debounced() {
    TEST_ASSERT_TRUE(Esp32BaseAppEvents::begin());
    Esp32BaseAppEvents::ConditionStateTracker tracker(1, 1000, 2000);
    const Esp32BaseAppEvents::EventInput activated = conditionEvent(5001, 1);
    const Esp32BaseAppEvents::EventInput recovered = conditionEvent(5002, 2);

    TEST_ASSERT_EQUAL(Esp32BaseAppEvents::ConditionObservationResult::ActivationConfirmationPending,
                      Esp32BaseAppEvents::observeConditionState(
                          tracker, Esp32BaseAppEvents::ObservedConditionState::Active, activated));
    nativeMillisValue() = 999;
    TEST_ASSERT_EQUAL(Esp32BaseAppEvents::ConditionObservationResult::ActivationConfirmationPending,
                      Esp32BaseAppEvents::observeConditionState(
                          tracker, Esp32BaseAppEvents::ObservedConditionState::Active, activated));
    nativeMillisValue() = 1000;
    TEST_ASSERT_EQUAL(Esp32BaseAppEvents::ConditionObservationResult::ActivationEventStored,
                      Esp32BaseAppEvents::observeConditionState(
                          tracker, Esp32BaseAppEvents::ObservedConditionState::Active, activated));
    TEST_ASSERT_EQUAL_UINT32(1, Esp32BaseAppEvents::eventCount());
    TEST_ASSERT_EQUAL_UINT32(1, g_conditionStateWriteCount);

    nativeMillisValue() = 2000;
    TEST_ASSERT_EQUAL(Esp32BaseAppEvents::ConditionObservationResult::ConditionUnchanged,
                      Esp32BaseAppEvents::observeConditionState(
                          tracker, Esp32BaseAppEvents::ObservedConditionState::Active, activated));
    TEST_ASSERT_EQUAL_UINT32(1, Esp32BaseAppEvents::eventCount());
    TEST_ASSERT_EQUAL_UINT32(1, g_conditionStateWriteCount);

    nativeMillisValue() = 3000;
    TEST_ASSERT_EQUAL(Esp32BaseAppEvents::ConditionObservationResult::RecoveryConfirmationPending,
                      Esp32BaseAppEvents::observeConditionState(
                          tracker, Esp32BaseAppEvents::ObservedConditionState::Inactive, recovered));
    nativeMillisValue() = 4999;
    TEST_ASSERT_EQUAL(Esp32BaseAppEvents::ConditionObservationResult::RecoveryConfirmationPending,
                      Esp32BaseAppEvents::observeConditionState(
                          tracker, Esp32BaseAppEvents::ObservedConditionState::Inactive, recovered));
    nativeMillisValue() = 5000;
    TEST_ASSERT_EQUAL(Esp32BaseAppEvents::ConditionObservationResult::RecoveryEventStored,
                      Esp32BaseAppEvents::observeConditionState(
                          tracker, Esp32BaseAppEvents::ObservedConditionState::Inactive, recovered));

    nativeMillisValue() = 6000;
    TEST_ASSERT_EQUAL(Esp32BaseAppEvents::ConditionObservationResult::ActivationConfirmationPending,
                      Esp32BaseAppEvents::observeConditionState(
                          tracker, Esp32BaseAppEvents::ObservedConditionState::Active, activated));
    nativeMillisValue() = 7000;
    TEST_ASSERT_EQUAL(Esp32BaseAppEvents::ConditionObservationResult::ActivationEventStored,
                      Esp32BaseAppEvents::observeConditionState(
                          tracker, Esp32BaseAppEvents::ObservedConditionState::Active, activated));
    TEST_ASSERT_EQUAL_UINT32(3, Esp32BaseAppEvents::eventCount());
    TEST_ASSERT_EQUAL_UINT32(3, g_conditionStateWriteCount);

    EventCollector collected;
    TEST_ASSERT_TRUE(Esp32BaseAppEvents::readLatest(0, 3, collectEvent, &collected));
    TEST_ASSERT_EQUAL(Esp32BaseAppEvents::EventKind::ConditionActivated, collected.events[0].eventKind);
    TEST_ASSERT_EQUAL(Esp32BaseAppEvents::EventKind::ConditionRecovered, collected.events[1].eventKind);
    TEST_ASSERT_EQUAL(Esp32BaseAppEvents::EventKind::ConditionActivated, collected.events[2].eventKind);
    TEST_ASSERT_EQUAL_UINT8(1, collected.events[0].conditionId);
}

void test_unknown_observation_cancels_unconfirmed_transition() {
    TEST_ASSERT_TRUE(Esp32BaseAppEvents::begin());
    Esp32BaseAppEvents::ConditionStateTracker tracker(2, 1000, 1000);
    const Esp32BaseAppEvents::EventInput event = conditionEvent(5101);
    TEST_ASSERT_EQUAL(Esp32BaseAppEvents::ConditionObservationResult::ActivationConfirmationPending,
                      Esp32BaseAppEvents::observeConditionState(
                          tracker, Esp32BaseAppEvents::ObservedConditionState::Active, event));
    nativeMillisValue() = 500;
    TEST_ASSERT_EQUAL(Esp32BaseAppEvents::ConditionObservationResult::ObservationUnknown,
                      Esp32BaseAppEvents::observeConditionState(
                          tracker, Esp32BaseAppEvents::ObservedConditionState::Unknown, event));
    nativeMillisValue() = 1000;
    TEST_ASSERT_EQUAL(Esp32BaseAppEvents::ConditionObservationResult::ActivationConfirmationPending,
                      Esp32BaseAppEvents::observeConditionState(
                          tracker, Esp32BaseAppEvents::ObservedConditionState::Active, event));
    nativeMillisValue() = 1999;
    TEST_ASSERT_EQUAL(Esp32BaseAppEvents::ConditionObservationResult::ActivationConfirmationPending,
                      Esp32BaseAppEvents::observeConditionState(
                          tracker, Esp32BaseAppEvents::ObservedConditionState::Active, event));
    TEST_ASSERT_EQUAL_UINT32(0, Esp32BaseAppEvents::eventCount());
    TEST_ASSERT_EQUAL_UINT32(0, g_conditionStateWriteCount);
}

void test_different_condition_ids_do_not_interfere() {
    TEST_ASSERT_TRUE(Esp32BaseAppEvents::begin());
    Esp32BaseAppEvents::ConditionStateTracker first(1, 0, 0);
    Esp32BaseAppEvents::ConditionStateTracker second(32, 0, 0);
    const Esp32BaseAppEvents::EventInput event = conditionEvent(5201);
    TEST_ASSERT_EQUAL(Esp32BaseAppEvents::ConditionObservationResult::ActivationEventStored,
                      Esp32BaseAppEvents::observeConditionState(
                          first, Esp32BaseAppEvents::ObservedConditionState::Active, event));
    TEST_ASSERT_EQUAL(Esp32BaseAppEvents::ConditionObservationResult::ActivationEventStored,
                      Esp32BaseAppEvents::observeConditionState(
                          second, Esp32BaseAppEvents::ObservedConditionState::Active, event));
    Esp32BaseAppEvents::AppEventsStatus status;
    TEST_ASSERT_TRUE(Esp32BaseAppEvents::readStatus(status));
    TEST_ASSERT_EQUAL_UINT8(2, status.activeConditionCount);
    TEST_ASSERT_EQUAL(Esp32BaseAppEvents::ConditionObservationResult::RecoveryEventStored,
                      Esp32BaseAppEvents::observeConditionState(
                          first, Esp32BaseAppEvents::ObservedConditionState::Inactive, event));
    TEST_ASSERT_TRUE(Esp32BaseAppEvents::readStatus(status));
    TEST_ASSERT_EQUAL_UINT8(1, status.activeConditionCount);
    TEST_ASSERT_EQUAL_HEX32(0x80000000UL, g_persistedActiveConditionIdBits);
}

void test_condition_confirmation_handles_millis_wrap() {
    TEST_ASSERT_TRUE(Esp32BaseAppEvents::begin());
    Esp32BaseAppEvents::ConditionStateTracker tracker(3, 1000, 0);
    const Esp32BaseAppEvents::EventInput event = conditionEvent(5301);
    nativeMillisValue() = UINT32_MAX - 500U;
    TEST_ASSERT_EQUAL(Esp32BaseAppEvents::ConditionObservationResult::ActivationConfirmationPending,
                      Esp32BaseAppEvents::observeConditionState(
                          tracker, Esp32BaseAppEvents::ObservedConditionState::Active, event));
    nativeMillisValue() = 498;
    TEST_ASSERT_EQUAL(Esp32BaseAppEvents::ConditionObservationResult::ActivationConfirmationPending,
                      Esp32BaseAppEvents::observeConditionState(
                          tracker, Esp32BaseAppEvents::ObservedConditionState::Active, event));
    nativeMillisValue() = 499;
    TEST_ASSERT_EQUAL(Esp32BaseAppEvents::ConditionObservationResult::ActivationEventStored,
                      Esp32BaseAppEvents::observeConditionState(
                          tracker, Esp32BaseAppEvents::ObservedConditionState::Active, event));
}

void test_reboot_restores_active_condition_without_duplicate_event() {
    TEST_ASSERT_TRUE(Esp32BaseAppEvents::begin());
    const Esp32BaseAppEvents::EventInput event = conditionEvent(5401);
    Esp32BaseAppEvents::ConditionStateTracker beforeReboot(4, 0, 0);
    TEST_ASSERT_EQUAL(Esp32BaseAppEvents::ConditionObservationResult::ActivationEventStored,
                      Esp32BaseAppEvents::observeConditionState(
                          beforeReboot, Esp32BaseAppEvents::ObservedConditionState::Active, event));
    TEST_ASSERT_EQUAL_UINT32(1, Esp32BaseAppEvents::eventCount());

    TEST_ASSERT_TRUE(Esp32BaseAppEvents::begin());
    Esp32BaseAppEvents::ConditionStateTracker afterReboot(4, 0, 0);
    TEST_ASSERT_EQUAL(Esp32BaseAppEvents::ConditionObservationResult::ConditionUnchanged,
                      Esp32BaseAppEvents::observeConditionState(
                          afterReboot, Esp32BaseAppEvents::ObservedConditionState::Active, event));
    TEST_ASSERT_EQUAL_UINT32(1, Esp32BaseAppEvents::eventCount());
    TEST_ASSERT_EQUAL_UINT32(1, g_conditionStateWriteCount);
}

void test_condition_state_save_failure_blocks_until_reload_succeeds() {
    TEST_ASSERT_TRUE(Esp32BaseAppEvents::begin());
    Esp32BaseAppEvents::ConditionStateTracker tracker(5, 0, 0);
    const Esp32BaseAppEvents::EventInput event = conditionEvent(5501);
    g_conditionStateWriteFails = true;
    TEST_ASSERT_EQUAL(Esp32BaseAppEvents::ConditionObservationResult::EventStoredButConditionStateSaveFailed,
                      Esp32BaseAppEvents::observeConditionState(
                          tracker, Esp32BaseAppEvents::ObservedConditionState::Active, event));
    TEST_ASSERT_EQUAL_UINT32(1, Esp32BaseAppEvents::eventCount());
    TEST_ASSERT_EQUAL(Esp32BaseAppEvents::ConditionObservationResult::BlockedByPendingConditionStateSave,
                      Esp32BaseAppEvents::observeConditionState(
                          tracker, Esp32BaseAppEvents::ObservedConditionState::Active, event));
    TEST_ASSERT_FALSE(Esp32BaseAppEvents::reload());
    g_conditionStateWriteFails = false;
    TEST_ASSERT_TRUE(Esp32BaseAppEvents::reload());
    TEST_ASSERT_EQUAL_HEX32(1UL << 4U, g_persistedActiveConditionIdBits);
    TEST_ASSERT_EQUAL(Esp32BaseAppEvents::ConditionObservationResult::ConditionUnchanged,
                      Esp32BaseAppEvents::observeConditionState(
                          tracker, Esp32BaseAppEvents::ObservedConditionState::Active, event));
    TEST_ASSERT_EQUAL_UINT32(1, Esp32BaseAppEvents::eventCount());
}

void test_condition_state_load_failure_does_not_block_discrete_events() {
    g_conditionStateReadFails = true;
    TEST_ASSERT_FALSE(Esp32BaseAppEvents::begin());
    const Esp32BaseAppEvents::EventInput event = conditionEvent(5601);
    TEST_ASSERT_EQUAL(Esp32BaseAppEvents::DiscreteEventAppendResult::Stored,
                      Esp32BaseAppEvents::appendDiscreteEvent(event));
    Esp32BaseAppEvents::ConditionStateTracker tracker(6, 0, 0);
    TEST_ASSERT_EQUAL(Esp32BaseAppEvents::ConditionObservationResult::ConditionStateUnavailable,
                      Esp32BaseAppEvents::observeConditionState(
                          tracker, Esp32BaseAppEvents::ObservedConditionState::Active, event));
}

void test_event_store_failure_is_latched_without_retry_storm() {
    TEST_ASSERT_TRUE(Esp32BaseAppEvents::begin());
    Esp32BaseAppEvents::ConditionStateTracker tracker(7, 0, 0);
    const Esp32BaseAppEvents::EventInput event = conditionEvent(5701);
    g_fileSystemWriteFails = true;
    const uint32_t attemptsBefore = g_eventStoreWriteAttempts;
    TEST_ASSERT_EQUAL(Esp32BaseAppEvents::ConditionObservationResult::EventStoreWriteFailed,
                      Esp32BaseAppEvents::observeConditionState(
                          tracker, Esp32BaseAppEvents::ObservedConditionState::Active, event));
    const uint32_t attemptsAfterFailure = g_eventStoreWriteAttempts;
    TEST_ASSERT_TRUE(attemptsAfterFailure > attemptsBefore);
    TEST_ASSERT_EQUAL(Esp32BaseAppEvents::ConditionObservationResult::EventStoreUnavailable,
                      Esp32BaseAppEvents::observeConditionState(
                          tracker, Esp32BaseAppEvents::ObservedConditionState::Active, event));
    TEST_ASSERT_EQUAL_UINT32(attemptsAfterFailure, g_eventStoreWriteAttempts);
    TEST_ASSERT_EQUAL_UINT32(0, g_conditionStateWriteCount);
}

void test_clear_format_and_reload_preserve_confirmed_condition_state() {
    TEST_ASSERT_TRUE(Esp32BaseAppEvents::begin());
    Esp32BaseAppEvents::ConditionStateTracker tracker(8, 0, 0);
    const Esp32BaseAppEvents::EventInput event = conditionEvent(5801);
    TEST_ASSERT_EQUAL(Esp32BaseAppEvents::ConditionObservationResult::ActivationEventStored,
                      Esp32BaseAppEvents::observeConditionState(
                          tracker, Esp32BaseAppEvents::ObservedConditionState::Active, event));
    TEST_ASSERT_TRUE(Esp32BaseAppEvents::clearEventHistory());
    TEST_ASSERT_EQUAL_UINT32(0, Esp32BaseAppEvents::eventCount());
    Esp32BaseAppEvents::AppEventsStatus status;
    TEST_ASSERT_TRUE(Esp32BaseAppEvents::readStatus(status));
    TEST_ASSERT_EQUAL_UINT8(1, status.activeConditionCount);

    TEST_ASSERT_TRUE(Esp32BaseFs::format());
    TEST_ASSERT_TRUE(Esp32BaseAppEvents::reload());
    TEST_ASSERT_TRUE(Esp32BaseAppEvents::readStatus(status));
    TEST_ASSERT_EQUAL_UINT8(1, status.activeConditionCount);
    TEST_ASSERT_EQUAL(Esp32BaseAppEvents::ConditionObservationResult::ConditionUnchanged,
                      Esp32BaseAppEvents::observeConditionState(
                          tracker, Esp32BaseAppEvents::ObservedConditionState::Active, event));
    TEST_ASSERT_EQUAL_UINT32(0, Esp32BaseAppEvents::eventCount());
}

void test_condition_arguments_and_duplicate_ids_are_rejected() {
    TEST_ASSERT_TRUE(Esp32BaseAppEvents::begin());
    const Esp32BaseAppEvents::EventInput event = conditionEvent(5901);
    Esp32BaseAppEvents::ConditionStateTracker invalidId(0, 0, 0);
    TEST_ASSERT_EQUAL(Esp32BaseAppEvents::ConditionObservationResult::InvalidArgument,
                      Esp32BaseAppEvents::observeConditionState(
                          invalidId, Esp32BaseAppEvents::ObservedConditionState::Active, event));
    Esp32BaseAppEvents::ConditionStateTracker invalidDuration(
        1, Esp32BaseAppEvents::MAX_CONFIRMATION_MS + 1U, 0);
    TEST_ASSERT_EQUAL(Esp32BaseAppEvents::ConditionObservationResult::InvalidArgument,
                      Esp32BaseAppEvents::observeConditionState(
                          invalidDuration, Esp32BaseAppEvents::ObservedConditionState::Active, event));
    Esp32BaseAppEvents::ConditionStateTracker first(2, 0, 0);
    Esp32BaseAppEvents::ConditionStateTracker duplicate(2, 0, 0);
    TEST_ASSERT_EQUAL(Esp32BaseAppEvents::ConditionObservationResult::ConditionUnchanged,
                      Esp32BaseAppEvents::observeConditionState(
                          first, Esp32BaseAppEvents::ObservedConditionState::Inactive, event));
    TEST_ASSERT_EQUAL(Esp32BaseAppEvents::ConditionObservationResult::InvalidArgument,
                      Esp32BaseAppEvents::observeConditionState(
                          duplicate, Esp32BaseAppEvents::ObservedConditionState::Inactive, event));
}

void test_forget_condition_state_does_not_create_recovery_event() {
    TEST_ASSERT_TRUE(Esp32BaseAppEvents::begin());
    Esp32BaseAppEvents::ConditionStateTracker tracker(9, 0, 0);
    const Esp32BaseAppEvents::EventInput event = conditionEvent(6001);
    TEST_ASSERT_EQUAL(Esp32BaseAppEvents::ConditionObservationResult::ActivationEventStored,
                      Esp32BaseAppEvents::observeConditionState(
                          tracker, Esp32BaseAppEvents::ObservedConditionState::Active, event));
    TEST_ASSERT_TRUE(Esp32BaseAppEvents::forgetConditionState(9));
    TEST_ASSERT_EQUAL_UINT32(1, Esp32BaseAppEvents::eventCount());
    TEST_ASSERT_EQUAL_HEX32(0, g_persistedActiveConditionIdBits);
    Esp32BaseAppEvents::AppEventsStatus status;
    TEST_ASSERT_TRUE(Esp32BaseAppEvents::readStatus(status));
    TEST_ASSERT_EQUAL_UINT8(0, status.activeConditionCount);
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_create_calculates_capacity_and_store_budget);
    RUN_TEST(test_segment_rotation_reads_latest_first);
    RUN_TEST(test_smaller_budget_removes_oversized_old_segment_and_resets_visible_range);
    RUN_TEST(test_completed_record_uses_duration_and_resolves_time);
    RUN_TEST(test_corrupt_record_is_skipped_and_reported);
    RUN_TEST(test_corrupt_record_id_is_reported_and_read_as_corrupt);
    RUN_TEST(test_torn_tail_is_reserved_and_never_returned);
    RUN_TEST(test_corrupt_segment_header_is_skipped_but_store_remains_writable);
    RUN_TEST(test_tiny_corrupt_segment_reserves_its_filename_id);
    RUN_TEST(test_overlapping_segment_ranges_are_a_structural_fault);
    RUN_TEST(test_clear_hides_records_and_continues_ids);
    RUN_TEST(test_different_versions_use_different_directories);
    RUN_TEST(test_clearing_current_version_does_not_modify_historical_version);
    RUN_TEST(test_existing_control_wrong_size_is_not_rewritten);
    RUN_TEST(test_existing_definition_mismatch_is_not_rewritten);
    RUN_TEST(test_minimum_free_space_blocks_business_store_creation);
    RUN_TEST(test_latest_pagination_crosses_segments);
    RUN_TEST(test_torn_clear_header_falls_back_to_pre_clear_state);
    RUN_TEST(test_clear_boundary_stays_effective_when_segment_cleanup_fails);
    RUN_TEST(test_app_events_uses_compact_payload_and_default_budget);
    RUN_TEST(test_discrete_events_with_identical_values_are_each_stored);
    RUN_TEST(test_condition_activation_recovery_and_reactivation_are_debounced);
    RUN_TEST(test_unknown_observation_cancels_unconfirmed_transition);
    RUN_TEST(test_different_condition_ids_do_not_interfere);
    RUN_TEST(test_condition_confirmation_handles_millis_wrap);
    RUN_TEST(test_reboot_restores_active_condition_without_duplicate_event);
    RUN_TEST(test_condition_state_save_failure_blocks_until_reload_succeeds);
    RUN_TEST(test_condition_state_load_failure_does_not_block_discrete_events);
    RUN_TEST(test_event_store_failure_is_latched_without_retry_storm);
    RUN_TEST(test_clear_format_and_reload_preserve_confirmed_condition_state);
    RUN_TEST(test_condition_arguments_and_duplicate_ids_are_rejected);
    RUN_TEST(test_forget_condition_state_does_not_create_recovery_event);
    return UNITY_END();
}
