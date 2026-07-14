#include "../Esp32BaseProfile.h"

#if ESP32BASE_ENABLE_APP_EVENTS

#include "Esp32BaseAppEvents.h"
#include "../core/Esp32BaseLog.h"

#include <string.h>

namespace {

constexpr uint32_t kAppEventPayloadBytes = 24;
constexpr uint16_t kAppEventStoreVersion = 1;
constexpr const char* kAppEventRecordTypeName = "app-events";

Esp32BaseRecordStore g_appEventStore;

uint16_t appEventsReadU16(const uint8_t* data) {
    return static_cast<uint16_t>(data[0]) |
           static_cast<uint16_t>(static_cast<uint16_t>(data[1]) << 8);
}

uint32_t appEventsReadU32(const uint8_t* data) {
    return static_cast<uint32_t>(data[0]) |
           (static_cast<uint32_t>(data[1]) << 8) |
           (static_cast<uint32_t>(data[2]) << 16) |
           (static_cast<uint32_t>(data[3]) << 24);
}

void appEventsWriteU16(uint8_t* data, uint16_t value) {
    data[0] = static_cast<uint8_t>(value & 0xFFU);
    data[1] = static_cast<uint8_t>((value >> 8) & 0xFFU);
}

void appEventsWriteU32(uint8_t* data, uint32_t value) {
    data[0] = static_cast<uint8_t>(value & 0xFFUL);
    data[1] = static_cast<uint8_t>((value >> 8) & 0xFFUL);
    data[2] = static_cast<uint8_t>((value >> 16) & 0xFFUL);
    data[3] = static_cast<uint8_t>((value >> 24) & 0xFFUL);
}

bool appEventsValidLevel(Esp32BaseAppEvents::Level level) {
    return level == Esp32BaseAppEvents::Level::Info ||
           level == Esp32BaseAppEvents::Level::Warning ||
           level == Esp32BaseAppEvents::Level::Error;
}

void appEventsEncode(const Esp32BaseAppEvents::EventInput& event, uint8_t* payload) {
    appEventsWriteU32(payload, event.eventCode);
    appEventsWriteU32(payload + 4, event.reasonCode);
    appEventsWriteU32(payload + 8, event.objectId);
    appEventsWriteU32(payload + 12, static_cast<uint32_t>(event.value1));
    appEventsWriteU32(payload + 16, static_cast<uint32_t>(event.value2));
    appEventsWriteU16(payload + 20, event.flags);
    appEventsWriteU16(payload + 22, static_cast<uint16_t>(event.level));
}

bool appEventsDecode(const Esp32BaseRecordStore::RecordView& record,
                     Esp32BaseAppEvents::EventRecord& event) {
    if (!record.payload || record.payloadSizeBytes != kAppEventPayloadBytes) {
        return false;
    }
    event = Esp32BaseAppEvents::EventRecord();
    event.recordId = record.recordId;
    event.timing = record.timing;
    event.eventCode = appEventsReadU32(record.payload);
    event.reasonCode = appEventsReadU32(record.payload + 4);
    event.objectId = appEventsReadU32(record.payload + 8);
    event.value1 = static_cast<int32_t>(appEventsReadU32(record.payload + 12));
    event.value2 = static_cast<int32_t>(appEventsReadU32(record.payload + 16));
    event.flags = appEventsReadU16(record.payload + 20);
    event.level = static_cast<Esp32BaseAppEvents::Level>(appEventsReadU16(record.payload + 22));
    return event.eventCode != 0 && appEventsValidLevel(event.level);
}

struct AppEventsReadContext {
    Esp32BaseAppEvents::ReadCallback callback;
    void* user;
    uint32_t offset;
    uint32_t limit;
    uint32_t matched;
    uint32_t emitted;
};

void appEventsReadAdapter(const Esp32BaseRecordStore::RecordView& record, void* user) {
    AppEventsReadContext* context = static_cast<AppEventsReadContext*>(user);
    if (!context || !context->callback) {
        return;
    }
    Esp32BaseAppEvents::EventRecord event;
    if (!appEventsDecode(record, event)) return;
    const uint32_t index = context->matched++;
    if (index < context->offset || context->emitted >= context->limit) return;
    ++context->emitted;
    context->callback(event, context->user);
}

} // namespace

bool Esp32BaseAppEvents::begin() {
    Esp32BaseRecordStore::StoreDefinition definition;
    definition.recordTypeName = kAppEventRecordTypeName;
    definition.storeVersion = kAppEventStoreVersion;
    definition.payloadSizeBytes = kAppEventPayloadBytes;
    definition.maximumFileBytes = ESP32BASE_APP_EVENT_STORE_MAX_BYTES;
    definition.minimumFileSystemFreeBytes = 0;
    const bool ok = g_appEventStore.begin(definition);
    if (!ok) {
        ESP32BASE_LOG_W("app_events", "begin_failed error=%s", g_appEventStore.lastErrorReason());
    }
    return ok;
}

bool Esp32BaseAppEvents::reload() { return g_appEventStore.reload(); }

bool Esp32BaseAppEvents::append(const EventInput& event) {
    if (event.eventCode == 0 || !appEventsValidLevel(event.level)) {
        return false;
    }
    uint8_t payload[kAppEventPayloadBytes];
    appEventsEncode(event, payload);
    return g_appEventStore.appendInstant(payload, sizeof(payload));
}

bool Esp32BaseAppEvents::readLatest(uint32_t offset,
                                    uint32_t limit,
                                    ReadCallback callback,
                                    void* user) {
    if (!callback || limit == 0) return false;
    EventStoreStatus status;
    if (!readStatus(status) || !status.storage.ready) return false;
    if (status.storage.recordCount == 0) return true;
    uint8_t scratch[kAppEventPayloadBytes];
    AppEventsReadContext context = {callback, user, offset, limit, 0, 0};
    return g_appEventStore.readLatest(0,
                                      status.storage.recordCount,
                                      scratch,
                                      sizeof(scratch),
                                      appEventsReadAdapter,
                                      &context);
}

Esp32BaseAppEvents::EventReadResult Esp32BaseAppEvents::readById(uint32_t recordId,
                                                                 EventRecord& event) {
    uint8_t payload[kAppEventPayloadBytes];
    Esp32BaseRecordStore::RecordMetadata record;
    const Esp32BaseRecordStore::RecordReadResult result = g_appEventStore.readById(recordId, payload, sizeof(payload), record);
    if (result != Esp32BaseRecordStore::RecordReadResult::Found) {
        switch (result) {
            case Esp32BaseRecordStore::RecordReadResult::NotFound: return EventReadResult::NotFound;
            case Esp32BaseRecordStore::RecordReadResult::Corrupt: return EventReadResult::Corrupt;
            case Esp32BaseRecordStore::RecordReadResult::IoError: return EventReadResult::IoError;
            default: return EventReadResult::InvalidArgument;
        }
    }
    Esp32BaseRecordStore::RecordView view;
    view.recordId = record.recordId;
    view.timing = record.timing;
    view.payload = payload;
    view.payloadSizeBytes = sizeof(payload);
    return appEventsDecode(view, event) ? EventReadResult::Found : EventReadResult::Corrupt;
}

bool Esp32BaseAppEvents::clear() { return g_appEventStore.clear(); }

bool Esp32BaseAppEvents::readStatus(EventStoreStatus& status) {
    return g_appEventStore.readStatus(status.storage);
}

bool Esp32BaseAppEvents::isReady() { return g_appEventStore.isReady(); }
bool Esp32BaseAppEvents::isWritable() { return g_appEventStore.isWritable(); }

uint32_t Esp32BaseAppEvents::count() {
    EventStoreStatus status;
    return readStatus(status) ? status.storage.recordCount : 0;
}

uint32_t Esp32BaseAppEvents::capacity() {
    EventStoreStatus status;
    return readStatus(status) ? status.storage.capacity : 0;
}

const char* Esp32BaseAppEvents::path() { return g_appEventStore.path(); }
const char* Esp32BaseAppEvents::lastErrorReason() { return g_appEventStore.lastErrorReason(); }

const char* Esp32BaseAppEvents::levelName(Level level) {
    switch (level) {
        case Level::Info: return "info";
        case Level::Warning: return "warning";
        case Level::Error: return "error";
        default: return "unknown";
    }
}

#endif
