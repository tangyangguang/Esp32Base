#include "../Esp32BaseProfile.h"

#if ESP32BASE_ENABLE_APP_EVENTS

#include "Esp32BaseAppEventLog.h"
#include "Esp32BaseFs.h"
#include "../core/Esp32BaseLog.h"
#include "../core/Esp32BaseSystem.h"
#include "../core/Esp32BaseUtil.h"
#if ESP32BASE_ENABLE_WATCHDOG
#include "Esp32BaseWatchdog.h"
#endif

#include <esp_timer.h>
#include <string.h>

namespace {
constexpr const char* kAppEventDir = "/app";
constexpr const char* kAppEventPath = "/app/events.bin";
constexpr uint32_t kAppEventHeaderMagic = 0x41504548UL; // APEH
constexpr uint32_t kAppEventRecordMagic = 0x41504552UL; // APER
constexpr uint16_t kAppEventVersion = 1;
constexpr uint16_t kAppEventHeaderCopies = 2;
constexpr uint16_t kAppEventCapacity = ESP32BASE_APP_EVENT_LOG_CAPACITY;
constexpr uint16_t kAppEventRecordSize = sizeof(Esp32BaseAppEventRecord);

struct Esp32BaseAppEventHeader {
    uint32_t magic;
    uint16_t version;
    uint16_t headerSize;
    uint16_t recordSize;
    uint16_t capacity;
    uint16_t head;
    uint16_t count;
    uint32_t nextId;
    uint32_t sequence;
    uint32_t crc32;
    uint8_t reserved[36];
};

static_assert(sizeof(Esp32BaseAppEventHeader) == 64, "Esp32BaseAppEventHeader layout changed");

bool g_appEventReady = false;
bool g_appEventFault = false;
uint16_t g_appEventHead = 0;
uint16_t g_appEventCount = 0;
uint16_t g_appEventValidCount = 0;
uint32_t g_appEventNextId = 1;
uint32_t g_appEventSequence = 0;
uint8_t g_appEventActiveHeader = 0;
char g_appEventLastError[48] = "";
Esp32BaseAppEventLog::TimeProvider g_appEventTimeProvider = nullptr;

uint32_t appEventStoreSize() {
    return static_cast<uint32_t>(kAppEventHeaderCopies * sizeof(Esp32BaseAppEventHeader)) +
           static_cast<uint32_t>(kAppEventCapacity) * static_cast<uint32_t>(kAppEventRecordSize);
}

uint32_t appEventRecordOffset(uint16_t slot) {
    return static_cast<uint32_t>(kAppEventHeaderCopies * sizeof(Esp32BaseAppEventHeader)) +
           static_cast<uint32_t>(slot) * static_cast<uint32_t>(kAppEventRecordSize);
}

void appEventSetError(const char* error) {
    esp32base_internal::copySafe(g_appEventLastError, sizeof(g_appEventLastError), error ? error : "error");
}

void appEventClearError() {
    g_appEventLastError[0] = '\0';
}

void appEventMarkFault(const char* error) {
    appEventSetError(error);
    g_appEventReady = false;
    g_appEventFault = true;
    ESP32BASE_LOG_W("app_events", "fault error=%s", g_appEventLastError);
}

bool appEventBeginFsOperation() {
#if ESP32BASE_ENABLE_WATCHDOG
    if (Esp32BaseWatchdog::currentTaskRemovedForLongOperation()) {
        return false;
    }
    return Esp32BaseWatchdog::removeCurrentTaskForLongOperation();
#else
    return false;
#endif
}

void appEventEndFsOperation(bool active) {
#if ESP32BASE_ENABLE_WATCHDOG
    if (active) {
        Esp32BaseWatchdog::restoreCurrentTaskAfterLongOperation();
    }
#else
    (void)active;
#endif
    yield();
}

uint32_t appEventCrc32(const uint8_t* data, size_t len) {
    uint32_t crc = 0xFFFFFFFFUL;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xEDB88320UL & (0UL - (crc & 1UL)));
        }
    }
    return ~crc;
}

uint16_t appEventCrc16(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFFU;
    for (size_t i = 0; i < len; ++i) {
        crc ^= static_cast<uint16_t>(data[i]);
        for (uint8_t bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ static_cast<uint16_t>(0xA001U & (0U - (crc & 1U)));
        }
    }
    return crc;
}

uint16_t appEventRecordCrc(const Esp32BaseAppEventRecord& record) {
    Esp32BaseAppEventRecord tmp;
    memcpy(&tmp, &record, sizeof(tmp));
    tmp.crc16 = 0;
    return appEventCrc16(reinterpret_cast<const uint8_t*>(&tmp), sizeof(tmp));
}

uint32_t appEventHeaderCrc(const Esp32BaseAppEventHeader& header) {
    Esp32BaseAppEventHeader tmp;
    memcpy(&tmp, &header, sizeof(tmp));
    tmp.crc32 = 0;
    return appEventCrc32(reinterpret_cast<const uint8_t*>(&tmp), sizeof(tmp));
}

bool appEventReadExact(uint32_t offset, void* out, size_t len) {
    if (!out) {
        return false;
    }
    size_t readLen = 0;
    const bool watchdogReleased = appEventBeginFsOperation();
    const bool ok = Esp32BaseFs::readBytesAt(kAppEventPath,
                                             offset,
                                             reinterpret_cast<uint8_t*>(out),
                                             len,
                                             &readLen) &&
                    readLen == len;
    appEventEndFsOperation(watchdogReleased);
    return ok;
}

bool appEventWriteExact(uint32_t offset, const void* data, size_t len) {
    if (!data && len > 0) {
        return false;
    }
    const bool watchdogReleased = appEventBeginFsOperation();
    const bool ok = Esp32BaseFs::writeBytesAt(kAppEventPath,
                                              offset,
                                              reinterpret_cast<const uint8_t*>(data),
                                              len);
    appEventEndFsOperation(watchdogReleased);
    return ok;
}

bool appEventRemoveStoreFile() {
    const bool watchdogReleased = appEventBeginFsOperation();
    const bool ok = Esp32BaseFs::removeFile(kAppEventPath);
    appEventEndFsOperation(watchdogReleased);
    return ok;
}

bool appEventEnsureDir() {
    if (!Esp32BaseFs::isReady()) {
        appEventSetError("fs_not_ready");
        return false;
    }
    if (!Esp32BaseFs::exists(kAppEventDir) &&
        !Esp32BaseFs::mkdir(kAppEventDir) &&
        !Esp32BaseFs::exists(kAppEventDir)) {
        appEventMarkFault("dir_create_failed");
        return false;
    }
    return true;
}

bool appEventCreateStoreFile() {
    const bool watchdogReleased = appEventBeginFsOperation();
    const bool ok = Esp32BaseFs::createFixedFile(kAppEventPath, appEventStoreSize(), 0);
    appEventEndFsOperation(watchdogReleased);
    return ok;
}

bool appEventValidHeader(const Esp32BaseAppEventHeader& header) {
    return header.magic == kAppEventHeaderMagic &&
           header.version == kAppEventVersion &&
           header.headerSize == sizeof(Esp32BaseAppEventHeader) &&
           header.recordSize == kAppEventRecordSize &&
           header.capacity == kAppEventCapacity &&
           header.head < kAppEventCapacity &&
           header.count <= kAppEventCapacity &&
           header.nextId != 0 &&
           header.crc32 == appEventHeaderCrc(header);
}

bool appEventValidRecord(const Esp32BaseAppEventRecord& record) {
    return record.magic == kAppEventRecordMagic &&
           record.id != 0 &&
           record.level >= Esp32BaseAppEventLog::LEVEL_INFO &&
           record.level <= Esp32BaseAppEventLog::LEVEL_ERROR &&
           record.crc16 == appEventRecordCrc(record);
}

bool appEventRecordIdBeforeNext(uint32_t id, uint32_t nextId) {
    return static_cast<int32_t>(id - nextId) < 0;
}

bool appEventCommittedForHeader(const Esp32BaseAppEventRecord& record, const Esp32BaseAppEventHeader& header) {
    return appEventValidRecord(record) && appEventRecordIdBeforeNext(record.id, header.nextId);
}

enum AppEventRecordStatus : uint8_t {
    APP_EVENT_RECORD_OK,
    APP_EVENT_RECORD_CORRUPT,
    APP_EVENT_RECORD_UNCOMMITTED,
    APP_EVENT_RECORD_READ_FAILED
};

AppEventRecordStatus appEventReadRecordForNextId(uint16_t slot, uint32_t nextId, Esp32BaseAppEventRecord& record) {
    if (!appEventReadExact(appEventRecordOffset(slot), &record, sizeof(record))) {
        return APP_EVENT_RECORD_READ_FAILED;
    }
    if (!appEventValidRecord(record)) {
        return APP_EVENT_RECORD_CORRUPT;
    }
    if (!appEventRecordIdBeforeNext(record.id, nextId)) {
        return APP_EVENT_RECORD_UNCOMMITTED;
    }
    return APP_EVENT_RECORD_OK;
}

void appEventInspectRecord(uint16_t slot, uint16_t index, Esp32BaseAppEventLog::StoreRecord& item) {
    memset(&item, 0, sizeof(item));
    item.slot = slot;
    item.index = index;
    item.offset = appEventRecordOffset(slot);
    item.status = Esp32BaseAppEventLog::STORE_RECORD_READ_FAILED;
    if (!appEventReadExact(item.offset, &item.record, sizeof(item.record))) {
        return;
    }
    item.readOk = true;
    item.storedCrc16 = item.record.crc16;
    item.calculatedCrc16 = appEventRecordCrc(item.record);
    item.magicOk = item.record.magic == kAppEventRecordMagic;
    item.levelOk = item.record.level >= Esp32BaseAppEventLog::LEVEL_INFO &&
                   item.record.level <= Esp32BaseAppEventLog::LEVEL_ERROR;
    item.crcOk = item.storedCrc16 == item.calculatedCrc16;
    item.committed = item.record.id != 0 && appEventRecordIdBeforeNext(item.record.id, g_appEventNextId);

    if (item.record.magic == 0 && item.record.id == 0 && item.storedCrc16 == 0) {
        item.status = Esp32BaseAppEventLog::STORE_RECORD_EMPTY;
    } else if (!item.magicOk) {
        item.status = Esp32BaseAppEventLog::STORE_RECORD_INVALID_MAGIC;
    } else if (!item.levelOk) {
        item.status = Esp32BaseAppEventLog::STORE_RECORD_INVALID_LEVEL;
    } else if (!item.crcOk) {
        item.status = Esp32BaseAppEventLog::STORE_RECORD_CRC_MISMATCH;
    } else if (!item.committed) {
        item.status = Esp32BaseAppEventLog::STORE_RECORD_UNCOMMITTED;
    } else {
        item.status = Esp32BaseAppEventLog::STORE_RECORD_OK;
    }
}

uint16_t appEventOldestSlot(uint16_t head, uint16_t count) {
    return static_cast<uint16_t>((static_cast<uint32_t>(head) + kAppEventCapacity - count) % kAppEventCapacity);
}

void appEventApplyHeader(const Esp32BaseAppEventHeader& header, uint8_t copyIndex) {
    g_appEventHead = header.head;
    g_appEventCount = header.count;
    g_appEventValidCount = header.count;
    g_appEventNextId = header.nextId;
    g_appEventSequence = header.sequence;
    g_appEventActiveHeader = copyIndex;
    g_appEventReady = true;
    g_appEventFault = false;
    g_appEventLastError[0] = '\0';
}

void appEventMakeHeader(Esp32BaseAppEventHeader& header,
                        uint16_t head,
                        uint16_t count,
                        uint32_t nextId,
                        uint32_t sequence) {
    memset(&header, 0, sizeof(header));
    header.magic = kAppEventHeaderMagic;
    header.version = kAppEventVersion;
    header.headerSize = sizeof(Esp32BaseAppEventHeader);
    header.recordSize = kAppEventRecordSize;
    header.capacity = kAppEventCapacity;
    header.head = head;
    header.count = count;
    header.nextId = nextId ? nextId : 1;
    header.sequence = sequence;
    header.crc32 = appEventHeaderCrc(header);
}

bool appEventWriteHeaderCopy(uint8_t copyIndex, const Esp32BaseAppEventHeader& header) {
    if (copyIndex >= kAppEventHeaderCopies || !appEventValidHeader(header)) {
        return false;
    }
    const uint32_t offset = static_cast<uint32_t>(copyIndex) * static_cast<uint32_t>(sizeof(Esp32BaseAppEventHeader));
    if (!appEventWriteExact(offset, &header, sizeof(header))) {
        return false;
    }
    Esp32BaseAppEventHeader verify;
    return appEventReadExact(offset, &verify, sizeof(verify)) &&
           appEventValidHeader(verify) &&
           verify.sequence == header.sequence &&
           verify.nextId == header.nextId &&
           verify.head == header.head &&
           verify.count == header.count;
}

bool appEventWriteEmptyStore(uint32_t nextId, uint32_t sequenceBase) {
    Esp32BaseAppEventHeader headerA;
    Esp32BaseAppEventHeader headerB;
    // Header A starts as active; Header B is valid but older for power-loss recovery.
    appEventMakeHeader(headerA, 0, 0, nextId ? nextId : 1, sequenceBase + 1U);
    appEventMakeHeader(headerB, 0, 0, nextId ? nextId : 1, sequenceBase);
    if (!appEventWriteHeaderCopy(0, headerA) || !appEventWriteHeaderCopy(1, headerB)) {
        return false;
    }
    appEventApplyHeader(headerA, 0);
    return true;
}

bool appEventReconcileLoadedStore(const Esp32BaseAppEventHeader& loadedHeader, uint8_t copyIndex) {
    Esp32BaseAppEventHeader header;
    memcpy(&header, &loadedHeader, sizeof(header));
    uint16_t droppedLeading = 0;

    while (header.count > 0) {
        const uint16_t oldest = appEventOldestSlot(header.head, header.count);
        Esp32BaseAppEventRecord record;
        const AppEventRecordStatus status = appEventReadRecordForNextId(oldest, header.nextId, record);
        if (status == APP_EVENT_RECORD_OK) {
            break;
        }
        --header.count;
        ++droppedLeading;
    }

    uint16_t validRecords = 0;
    uint16_t damagedRecords = 0;
    for (uint16_t i = 0; i < header.count; ++i) {
        const uint16_t slot = static_cast<uint16_t>((static_cast<uint32_t>(header.head) + kAppEventCapacity - header.count + i) % kAppEventCapacity);
        Esp32BaseAppEventRecord record;
        const AppEventRecordStatus status = appEventReadRecordForNextId(slot, header.nextId, record);
        if (status == APP_EVENT_RECORD_OK) {
            ++validRecords;
        } else {
            ++damagedRecords;
        }
        yield();
    }

    uint8_t activeHeader = copyIndex;
    if (droppedLeading > 0) {
        Esp32BaseAppEventHeader repaired;
        appEventMakeHeader(repaired, header.head, header.count, header.nextId, header.sequence + 1U);
        activeHeader = copyIndex == 0 ? 1 : 0;
        if (!appEventWriteHeaderCopy(activeHeader, repaired)) {
            appEventMarkFault("header_repair_failed");
            return false;
        }
        memcpy(&header, &repaired, sizeof(header));
    }

    appEventApplyHeader(header, activeHeader);
    g_appEventValidCount = validRecords;
    if (droppedLeading > 0 || damagedRecords > 0) {
        appEventSetError("record_skipped");
        ESP32BASE_LOG_W("app_events",
                        "store_reconciled dropped=%u damaged=%u span=%u valid=%u next_id=%lu",
                        static_cast<unsigned>(droppedLeading),
                        static_cast<unsigned>(damagedRecords),
                        static_cast<unsigned>(g_appEventCount),
                        static_cast<unsigned>(g_appEventValidCount),
                        static_cast<unsigned long>(g_appEventNextId));
    }
    return true;
}

bool appEventLoadHeader() {
    Esp32BaseAppEventHeader headerA;
    Esp32BaseAppEventHeader headerB;
    const bool readA = appEventReadExact(0, &headerA, sizeof(headerA));
    const bool readB = appEventReadExact(sizeof(Esp32BaseAppEventHeader), &headerB, sizeof(headerB));
    const bool validA = readA && appEventValidHeader(headerA);
    const bool validB = readB && appEventValidHeader(headerB);
    if (!validA && !validB) {
        return false;
    }
    if (validA && (!validB || static_cast<int32_t>(headerA.sequence - headerB.sequence) >= 0)) {
        return appEventReconcileLoadedStore(headerA, 0);
    }
    return appEventReconcileLoadedStore(headerB, 1);
}

bool appEventEnsureStoreFile() {
    if (!appEventEnsureDir()) {
        return false;
    }

    const uint32_t expectedSize = appEventStoreSize();
    const int64_t actualSize = Esp32BaseFs::fileSize(kAppEventPath);
    if (actualSize < 0) {
        if (Esp32BaseFs::exists(kAppEventPath)) {
            appEventMarkFault("store_unreadable");
            return false;
        }
        if (!appEventCreateStoreFile()) {
            appEventMarkFault("create_failed");
            return false;
        }
        if (!appEventWriteEmptyStore(1, 0)) {
            appEventMarkFault("header_write_failed");
            return false;
        }
        ESP32BASE_LOG_I("app_events",
                        "store_created path=%s records=%u bytes=%lu",
                        kAppEventPath,
                        static_cast<unsigned>(kAppEventCapacity),
                        static_cast<unsigned long>(expectedSize));
        return true;
    }

    if (actualSize == static_cast<int64_t>(expectedSize)) {
        if (appEventLoadHeader()) {
            return true;
        }
        if (g_appEventFault) {
            return false;
        }
        appEventMarkFault("header_invalid");
        return false;
    }

    appEventMarkFault("size_mismatch");
    ESP32BASE_LOG_W("app_events",
                    "store_size_mismatch path=%s actual=%lld expected=%lu",
                    kAppEventPath,
                    static_cast<long long>(actualSize),
                    static_cast<unsigned long>(expectedSize));
    return false;
}

bool appEventTokenChar(char c) {
    return (c >= 'a' && c <= 'z') ||
           (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') ||
           c == '_' || c == '-' || c == '.' || c == ':' || c == '/' || c == '@' || c == '#';
}

bool appEventValidateToken(const char* value, size_t fieldLen, bool required) {
    if (!value || !value[0]) {
        return !required;
    }
    size_t len = 0;
    for (const char* p = value; *p; ++p) {
        if (!appEventTokenChar(*p)) {
            return false;
        }
        ++len;
        if (len >= fieldLen) {
            return false;
        }
    }
    return true;
}

void appEventCopyToken(char* dst, size_t len, const char* src) {
    if (!dst || len == 0) {
        return;
    }
    memset(dst, 0, len);
    if (src) {
        strlcpy(dst, src, len);
    }
}

bool appEventUtf8Continuation(const char* p, size_t len) {
    for (size_t i = 1; i < len; ++i) {
        if (p[i] == '\0') {
            return false;
        }
        const uint8_t c = static_cast<uint8_t>(p[i]);
        if ((c & 0xC0U) != 0x80U) {
            return false;
        }
    }
    return true;
}

bool appEventCopyText(char* dst, size_t len, const char* src) {
    if (!dst || len == 0) {
        return false;
    }
    memset(dst, 0, len);
    if (!src || !src[0]) {
        return false;
    }
    size_t out = 0;
    const char* p = src;
    bool truncated = false;
    while (*p) {
        uint8_t c = static_cast<uint8_t>(*p);
        if (c < 0x20U) {
            if (out + 1U >= len) {
                truncated = true;
                break;
            }
            dst[out++] = ' ';
            ++p;
            continue;
        }

        size_t charLen = 1;
        if (c >= 0xC2U && c <= 0xDFU) {
            charLen = 2;
        } else if (c >= 0xE0U && c <= 0xEFU) {
            charLen = 3;
        } else if (c >= 0xF0U && c <= 0xF4U) {
            charLen = 4;
        } else if (c >= 0x80U) {
            c = '?';
            charLen = 1;
        }

        if (charLen > 1 && !appEventUtf8Continuation(p, charLen)) {
            c = '?';
            charLen = 1;
        }
        if (out + charLen >= len) {
            truncated = true;
            break;
        }
        if (charLen == 1) {
            dst[out++] = static_cast<char>(c);
            ++p;
        } else {
            memcpy(dst + out, p, charLen);
            out += charLen;
            p += charLen;
        }
    }
    dst[out] = '\0';
    return truncated || *p != '\0';
}

Esp32BaseAppEventLog::TimeSnapshot appEventDefaultTime() {
    Esp32BaseAppEventLog::TimeSnapshot value = {};
    value.synced = false;
    value.epochSec = 0;
    value.bootId = Esp32BaseSystem::bootCount();
    value.uptimeSec = static_cast<uint32_t>(esp_timer_get_time() / 1000000LL);
    return value;
}

Esp32BaseAppEventLog::TimeSnapshot appEventNow() {
    if (g_appEventTimeProvider) {
        return g_appEventTimeProvider();
    }
    return appEventDefaultTime();
}

void appEventUpdateValidCountAfterScan(uint16_t validRecords, uint16_t scannedRecords) {
    if (scannedRecords < g_appEventCount || validRecords == g_appEventValidCount) {
        return;
    }
    g_appEventValidCount = validRecords;
    ESP32BASE_LOG_W("app_events",
                    "valid_count_reconciled span=%u valid=%u",
                    static_cast<unsigned>(g_appEventCount),
                    static_cast<unsigned>(g_appEventValidCount));
}

bool appEventFillRecord(const Esp32BaseAppEventLog::Event& event, Esp32BaseAppEventRecord& record) {
    if (event.level < Esp32BaseAppEventLog::LEVEL_INFO || event.level > Esp32BaseAppEventLog::LEVEL_ERROR) {
        appEventSetError("invalid_level");
        return false;
    }
    if (!appEventValidateToken(event.source, sizeof(record.source), true) ||
        !appEventValidateToken(event.type, sizeof(record.type), true) ||
        !appEventValidateToken(event.reason, sizeof(record.reason), false) ||
        !appEventValidateToken(event.object, sizeof(record.object), false)) {
        appEventSetError("invalid_token");
        return false;
    }

    memset(&record, 0, sizeof(record));
    const Esp32BaseAppEventLog::TimeSnapshot now = appEventNow();
    record.magic = kAppEventRecordMagic;
    record.id = g_appEventNextId ? g_appEventNextId : 1;
    record.epochSec = now.synced ? now.epochSec : 0;
    record.bootId = now.bootId;
    record.uptimeSec = now.uptimeSec;
    record.value1 = event.value1;
    record.value2 = event.value2;
    record.value3 = event.value3;
    record.code = event.code;
    record.level = event.level;
    record.flags = now.synced ? Esp32BaseAppEventLog::FLAG_TIME_SYNCED : 0;
    record.valueMask = event.valueMask & (Esp32BaseAppEventLog::VALUE1 |
                                          Esp32BaseAppEventLog::VALUE2 |
                                          Esp32BaseAppEventLog::VALUE3);
    appEventCopyToken(record.source, sizeof(record.source), event.source);
    appEventCopyToken(record.type, sizeof(record.type), event.type);
    appEventCopyToken(record.reason, sizeof(record.reason), event.reason);
    appEventCopyToken(record.object, sizeof(record.object), event.object);
    if (appEventCopyText(record.text, sizeof(record.text), event.text)) {
        record.flags |= Esp32BaseAppEventLog::FLAG_TEXT_TRUNCATED;
    }
    record.crc16 = appEventRecordCrc(record);
    return true;
}
}

bool Esp32BaseAppEventLog::begin() {
    if (g_appEventReady) {
        appEventClearError();
        return true;
    }
    if (g_appEventFault) {
        return false;
    }
    if (!appEventEnsureStoreFile()) {
        return false;
    }
    ESP32BASE_LOG_D("app_events",
                    "store_ready path=%s records=%u count=%u next_id=%lu",
                    kAppEventPath,
                    static_cast<unsigned>(kAppEventCapacity),
                    static_cast<unsigned>(g_appEventCount),
                    static_cast<unsigned long>(g_appEventNextId));
    return true;
}

bool Esp32BaseAppEventLog::append(const Event& event) {
    if (!g_appEventReady && !begin()) {
        return false;
    }
    if (g_appEventFault || !g_appEventReady) {
        return false;
    }

    Esp32BaseAppEventRecord record;
    if (!appEventFillRecord(event, record)) {
        return false;
    }

    const uint16_t slot = g_appEventHead;
    bool overwritingCommitted = false;
    if (g_appEventCount >= kAppEventCapacity) {
        Esp32BaseAppEventRecord previous;
        if (appEventReadRecordForNextId(slot, g_appEventNextId, previous) == APP_EVENT_RECORD_OK) {
            overwritingCommitted = true;
        }
    }
    if (!appEventWriteExact(appEventRecordOffset(slot), &record, sizeof(record))) {
        appEventMarkFault("record_write_failed");
        return false;
    }
    Esp32BaseAppEventRecord verify;
    if (!appEventReadExact(appEventRecordOffset(slot), &verify, sizeof(verify)) ||
        !appEventValidRecord(verify) ||
        verify.id != record.id) {
        appEventMarkFault("record_verify_failed");
        return false;
    }

    const uint16_t nextHead = static_cast<uint16_t>((static_cast<uint32_t>(slot) + 1U) % kAppEventCapacity);
    const uint16_t nextCount = g_appEventCount < kAppEventCapacity ? static_cast<uint16_t>(g_appEventCount + 1U) : kAppEventCapacity;
    uint32_t nextId = record.id + 1U;
    if (nextId == 0) {
        nextId = 1;
    }

    Esp32BaseAppEventHeader nextHeader;
    appEventMakeHeader(nextHeader, nextHead, nextCount, nextId, g_appEventSequence + 1U);
    const uint8_t inactiveHeader = g_appEventActiveHeader == 0 ? 1 : 0;
    if (!appEventWriteHeaderCopy(inactiveHeader, nextHeader)) {
        appEventMarkFault("header_write_failed");
        return false;
    }

    uint16_t nextValidCount = g_appEventValidCount;
    if (g_appEventCount >= kAppEventCapacity && overwritingCommitted && nextValidCount > 0) {
        --nextValidCount;
    }
    if (nextValidCount < kAppEventCapacity) {
        ++nextValidCount;
    }
    appEventApplyHeader(nextHeader, inactiveHeader);
    g_appEventValidCount = nextValidCount;
    appEventClearError();
    return true;
}

bool Esp32BaseAppEventLog::readLatest(uint16_t offset, uint16_t limit, ReadCallback cb, void* user) {
    if (!cb) {
        return false;
    }
    if (g_appEventFault || !g_appEventReady) {
        if (!g_appEventFault) {
            appEventSetError("not_ready");
        }
        return false;
    }
    if (offset >= g_appEventValidCount || limit == 0) {
        appEventClearError();
        return true;
    }

    uint16_t emitted = 0;
    uint16_t validSeen = 0;
    uint16_t skipped = 0;
    uint16_t scanned = 0;
    for (uint16_t i = 0; i < g_appEventCount && emitted < limit; ++i) {
        scanned = static_cast<uint16_t>(i + 1U);
        const uint32_t distance = static_cast<uint32_t>(i) + 1U;
        const uint16_t slot = static_cast<uint16_t>((static_cast<uint32_t>(g_appEventHead) + kAppEventCapacity - distance) % kAppEventCapacity);
        Esp32BaseAppEventRecord record;
        const AppEventRecordStatus status = appEventReadRecordForNextId(slot, g_appEventNextId, record);
        if (status == APP_EVENT_RECORD_READ_FAILED) {
            appEventSetError("record_read_failed");
            return false;
        }
        if (status != APP_EVENT_RECORD_OK) {
            ++skipped;
            continue;
        }
        if (validSeen++ < offset) {
            continue;
        }
        cb(record, user);
        ++emitted;
        yield();
    }
    if (skipped > 0) {
        appEventSetError("record_skipped");
        ESP32BASE_LOG_W("app_events", "read_skipped_records count=%u", static_cast<unsigned>(skipped));
    } else {
        appEventClearError();
    }
    appEventUpdateValidCountAfterScan(validSeen, scanned);
    return true;
}

bool Esp32BaseAppEventLog::readStoreInfo(StoreInfo& info) {
    memset(&info, 0, sizeof(info));
    info.path = kAppEventPath;
    info.fileSize = Esp32BaseFs::isReady() ? Esp32BaseFs::fileSize(kAppEventPath) : -1;
    info.expectedFileSize = appEventStoreSize();
    info.headerSize = sizeof(Esp32BaseAppEventHeader);
    info.recordSize = kAppEventRecordSize;
    info.capacity = kAppEventCapacity;
    info.count = g_appEventCount;
    info.validCount = g_appEventValidCount;
    info.head = g_appEventHead;
    info.nextId = g_appEventNextId;
    info.sequence = g_appEventSequence;
    info.activeHeader = g_appEventActiveHeader;
    info.ready = g_appEventReady;
    info.faulted = g_appEventFault;
    if (!info.ready && !info.faulted) {
        appEventSetError("not_ready");
    } else if (info.ready && !info.faulted) {
        appEventClearError();
    }
    return info.ready && !info.faulted;
}

bool Esp32BaseAppEventLog::readStoreRecords(uint16_t offset, uint16_t limit, StoreRecordCallback cb, void* user) {
    if (!cb) {
        return false;
    }
    if (g_appEventFault || !g_appEventReady) {
        if (!g_appEventFault) {
            appEventSetError("not_ready");
        }
        return false;
    }
    if (offset >= g_appEventCount || limit == 0) {
        appEventClearError();
        return true;
    }

    uint16_t emitted = 0;
    for (uint16_t i = offset; i < g_appEventCount && emitted < limit; ++i) {
        const uint32_t distance = static_cast<uint32_t>(i) + 1U;
        const uint16_t slot = static_cast<uint16_t>((static_cast<uint32_t>(g_appEventHead) + kAppEventCapacity - distance) % kAppEventCapacity);
        StoreRecord item;
        appEventInspectRecord(slot, i, item);
        cb(item, user);
        ++emitted;
        yield();
    }
    appEventClearError();
    return true;
}

bool Esp32BaseAppEventLog::clear() {
    if (!Esp32BaseFs::isReady()) {
        appEventSetError("fs_not_ready");
        return false;
    }
    if (!appEventEnsureDir()) {
        return false;
    }
    const uint32_t preserveNextId = g_appEventNextId ? g_appEventNextId : 1;
    const uint32_t sequenceBase = g_appEventSequence + 1U;
    if (Esp32BaseFs::exists(kAppEventPath) && !appEventRemoveStoreFile()) {
        appEventMarkFault("remove_failed");
        return false;
    }
    if (!appEventCreateStoreFile()) {
        appEventMarkFault("create_failed");
        return false;
    }
    if (!appEventWriteEmptyStore(preserveNextId, sequenceBase)) {
        appEventMarkFault("header_write_failed");
        return false;
    }
    ESP32BASE_LOG_W("app_events", "store_cleared path=%s next_id=%lu", kAppEventPath, static_cast<unsigned long>(g_appEventNextId));
    appEventClearError();
    return true;
}

void Esp32BaseAppEventLog::setTimeProvider(TimeProvider provider) {
    g_appEventTimeProvider = provider;
}

bool Esp32BaseAppEventLog::isReady() {
    return g_appEventReady;
}

bool Esp32BaseAppEventLog::faulted() {
    return g_appEventFault;
}

const char* Esp32BaseAppEventLog::lastError() {
    return g_appEventLastError;
}

uint16_t Esp32BaseAppEventLog::count() {
    return g_appEventValidCount;
}

uint16_t Esp32BaseAppEventLog::capacity() {
    return kAppEventCapacity;
}

uint32_t Esp32BaseAppEventLog::nextId() {
    return g_appEventNextId ? g_appEventNextId : 1;
}

const char* Esp32BaseAppEventLog::path() {
    return kAppEventPath;
}

const char* Esp32BaseAppEventLog::levelName(Level level) {
    switch (level) {
        case LEVEL_INFO: return "info";
        case LEVEL_WARN: return "warn";
        case LEVEL_ERROR: return "error";
        default: return "unknown";
    }
}

const char* Esp32BaseAppEventLog::storeRecordStatusName(StoreRecordStatus status) {
    switch (status) {
        case STORE_RECORD_OK: return "ok";
        case STORE_RECORD_EMPTY: return "empty";
        case STORE_RECORD_INVALID_MAGIC: return "invalid_magic";
        case STORE_RECORD_INVALID_LEVEL: return "invalid_level";
        case STORE_RECORD_CRC_MISMATCH: return "crc_mismatch";
        case STORE_RECORD_UNCOMMITTED: return "uncommitted";
        case STORE_RECORD_READ_FAILED:
        default: return "read_failed";
    }
}

#endif
