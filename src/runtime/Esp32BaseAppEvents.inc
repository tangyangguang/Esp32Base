#include "../Esp32BaseProfile.h"

#if ESP32BASE_ENABLE_APP_EVENTS

#include "Esp32BaseAppEvents.h"
#include "../core/Esp32BaseLog.h"
#if ESP32BASE_ENABLE_APP_EVENT_CONDITIONS
#include "../core/internal/Esp32BaseConfigInternal.h"
#endif

#include <string.h>

namespace {

constexpr uint32_t kAppEventPayloadBytes = 24;
constexpr uint16_t kAppEventStoreVersion = 2;
constexpr const char* kAppEventRecordTypeName = "app-events";

Esp32BaseRecordStore g_appEventStore;

#if ESP32BASE_ENABLE_APP_EVENT_CONDITIONS
constexpr const char* kAppEventConditionNamespace = "eb_app_events";
constexpr const char* kActiveConditionIdBitsKey = "active_id_bits";

uint32_t g_activeConditionIdBits = 0;
uint32_t g_registeredConditionIdBits = 0;
uint32_t g_conditionStateRevision = 1;
bool g_conditionStateLoaded = false;
bool g_conditionStateSavePending = false;
char g_conditionStateError[64] = "";
#endif

uint32_t appEventsReadU32(const uint8_t* data) {
    return static_cast<uint32_t>(data[0]) |
           (static_cast<uint32_t>(data[1]) << 8) |
           (static_cast<uint32_t>(data[2]) << 16) |
           (static_cast<uint32_t>(data[3]) << 24);
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

bool appEventsValidKind(Esp32BaseAppEvents::EventKind eventKind) {
    return eventKind == Esp32BaseAppEvents::EventKind::Discrete ||
           eventKind == Esp32BaseAppEvents::EventKind::ConditionActivated ||
           eventKind == Esp32BaseAppEvents::EventKind::ConditionRecovered;
}

bool appEventsValidInput(const Esp32BaseAppEvents::EventInput& event) {
    return event.eventCode != 0 && appEventsValidLevel(event.level);
}

void appEventsEncode(const Esp32BaseAppEvents::EventInput& event,
                     Esp32BaseAppEvents::EventKind eventKind,
                     uint8_t conditionId,
                     uint8_t* payload) {
    appEventsWriteU32(payload, event.eventCode);
    appEventsWriteU32(payload + 4, event.reasonCode);
    appEventsWriteU32(payload + 8, event.objectId);
    appEventsWriteU32(payload + 12, static_cast<uint32_t>(event.value1));
    appEventsWriteU32(payload + 16, static_cast<uint32_t>(event.value2));
    payload[20] = event.flags;
    payload[21] = static_cast<uint8_t>(event.level);
    payload[22] = static_cast<uint8_t>(eventKind);
    payload[23] = conditionId;
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
    event.flags = record.payload[20];
    event.level = static_cast<Esp32BaseAppEvents::Level>(record.payload[21]);
    event.eventKind = static_cast<Esp32BaseAppEvents::EventKind>(record.payload[22]);
    event.conditionId = record.payload[23];
    if (event.eventCode == 0 || !appEventsValidLevel(event.level) || !appEventsValidKind(event.eventKind)) {
        return false;
    }
    if (event.eventKind == Esp32BaseAppEvents::EventKind::Discrete) {
        return event.conditionId == 0;
    }
    return event.conditionId >= 1 && event.conditionId <= 32;
}

Esp32BaseAppEvents::DiscreteEventAppendResult appEventsAppend(
    const Esp32BaseAppEvents::EventInput& event,
    Esp32BaseAppEvents::EventKind eventKind,
    uint8_t conditionId) {
    if (!appEventsValidInput(event)) {
        return Esp32BaseAppEvents::DiscreteEventAppendResult::InvalidEvent;
    }
    if (!g_appEventStore.isReady() || !g_appEventStore.isWritable()) {
        return Esp32BaseAppEvents::DiscreteEventAppendResult::EventStoreUnavailable;
    }
    uint8_t payload[kAppEventPayloadBytes];
    appEventsEncode(event, eventKind, conditionId, payload);
    return g_appEventStore.appendInstant(payload, sizeof(payload))
               ? Esp32BaseAppEvents::DiscreteEventAppendResult::Stored
               : Esp32BaseAppEvents::DiscreteEventAppendResult::EventStoreWriteFailed;
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

#if ESP32BASE_ENABLE_APP_EVENT_CONDITIONS
uint32_t appEventsConditionBit(uint8_t conditionId) {
    return 1UL << (conditionId - 1U);
}

uint8_t appEventsCountBits(uint32_t bits) {
    uint8_t count = 0;
    while (bits != 0) {
        count = static_cast<uint8_t>(count + (bits & 1U));
        bits >>= 1U;
    }
    return count;
}

void appEventsSetConditionError(const char* reason) {
    strlcpy(g_conditionStateError, reason ? reason : "", sizeof(g_conditionStateError));
}

void appEventsAdvanceConditionRevision() {
    ++g_conditionStateRevision;
    if (g_conditionStateRevision == 0) {
        g_conditionStateRevision = 1;
    }
    g_registeredConditionIdBits = 0;
}

bool appEventsLoadConditionState() {
    uint32_t storedBits = 0;
    const esp32base_internal::ConfigUInt32ReadResult result =
        esp32base_internal::readConfigUInt32(kAppEventConditionNamespace,
                                             kActiveConditionIdBitsKey,
                                             storedBits);
    if (result == esp32base_internal::ConfigUInt32ReadResult::Error) {
        g_conditionStateLoaded = false;
        appEventsSetConditionError("condition_state_load_failed");
        return false;
    }
    g_activeConditionIdBits = result == esp32base_internal::ConfigUInt32ReadResult::Found
                                  ? storedBits
                                  : 0;
    g_conditionStateLoaded = true;
    g_conditionStateSavePending = false;
    appEventsSetConditionError("");
    return true;
}

bool appEventsSaveConditionState() {
    if (!esp32base_internal::writeConfigUInt32(kAppEventConditionNamespace,
                                                kActiveConditionIdBitsKey,
                                                g_activeConditionIdBits)) {
        g_conditionStateSavePending = true;
        appEventsSetConditionError("condition_state_save_failed");
        return false;
    }
    g_conditionStateLoaded = true;
    g_conditionStateSavePending = false;
    appEventsSetConditionError("");
    return true;
}

#endif

} // namespace

#if ESP32BASE_ENABLE_APP_EVENT_CONDITIONS
Esp32BaseAppEvents::ConditionStateTracker::ConditionStateTracker(
    uint8_t conditionId,
    uint32_t activationConfirmationMs,
    uint32_t recoveryConfirmationMs)
    : activationConfirmationMs_(activationConfirmationMs),
      recoveryConfirmationMs_(recoveryConfirmationMs),
      pendingStartedMs_(0),
      stateRevision_(0),
      conditionId_(conditionId),
      pendingState_(ObservedConditionState::Unknown),
      registered_(false) {}

uint8_t Esp32BaseAppEvents::ConditionStateTracker::conditionId() const {
    return conditionId_;
}
#endif

bool Esp32BaseAppEvents::begin() {
    Esp32BaseRecordStore::StoreDefinition definition;
    definition.recordTypeName = kAppEventRecordTypeName;
    definition.storeVersion = kAppEventStoreVersion;
    definition.payloadSizeBytes = kAppEventPayloadBytes;
    definition.maximumStoreBytes = ESP32BASE_APP_EVENT_STORE_MAX_BYTES;
    definition.minimumFileSystemFreeBytes = 0;
    const bool storeOk = g_appEventStore.begin(definition);
    if (!storeOk) {
        ESP32BASE_LOG_W("app_events", "begin_failed error=%s", g_appEventStore.lastErrorReason());
    }
#if ESP32BASE_ENABLE_APP_EVENT_CONDITIONS
    g_activeConditionIdBits = 0;
    g_conditionStateLoaded = false;
    g_conditionStateSavePending = false;
    appEventsAdvanceConditionRevision();
    const bool conditionStateOk = appEventsLoadConditionState();
    if (!conditionStateOk) {
        ESP32BASE_LOG_W("app_events", "condition_state_begin_failed error=%s", g_conditionStateError);
    }
    return storeOk && conditionStateOk;
#else
    return storeOk;
#endif
}

bool Esp32BaseAppEvents::reload() {
    const bool storeOk = g_appEventStore.reload();
#if ESP32BASE_ENABLE_APP_EVENT_CONDITIONS
    appEventsAdvanceConditionRevision();
    const bool conditionStateOk = g_conditionStateSavePending
                                      ? appEventsSaveConditionState()
                                      : appEventsLoadConditionState();
    return storeOk && conditionStateOk;
#else
    return storeOk;
#endif
}

Esp32BaseAppEvents::DiscreteEventAppendResult Esp32BaseAppEvents::appendDiscreteEvent(
    const EventInput& event) {
    return appEventsAppend(event, EventKind::Discrete, 0);
}

#if ESP32BASE_ENABLE_APP_EVENT_CONDITIONS
Esp32BaseAppEvents::ConditionObservationResult Esp32BaseAppEvents::observeConditionState(
    ConditionStateTracker& tracker,
    ObservedConditionState observedState,
    const EventInput& transitionEvent) {
    if (tracker.conditionId_ < MIN_CONDITION_ID || tracker.conditionId_ > MAX_CONDITION_ID ||
        tracker.activationConfirmationMs_ > MAX_CONFIRMATION_MS ||
        tracker.recoveryConfirmationMs_ > MAX_CONFIRMATION_MS ||
        (observedState != ObservedConditionState::Inactive &&
         observedState != ObservedConditionState::Active &&
         observedState != ObservedConditionState::Unknown)) {
        return ConditionObservationResult::InvalidArgument;
    }
    if (tracker.stateRevision_ != g_conditionStateRevision) {
        tracker.pendingState_ = ObservedConditionState::Unknown;
        tracker.pendingStartedMs_ = 0;
        tracker.registered_ = false;
        tracker.stateRevision_ = g_conditionStateRevision;
    }
    if (!tracker.registered_) {
        const uint32_t registrationBit = appEventsConditionBit(tracker.conditionId_);
        if ((g_registeredConditionIdBits & registrationBit) != 0) {
            return ConditionObservationResult::InvalidArgument;
        }
        g_registeredConditionIdBits |= registrationBit;
        tracker.registered_ = true;
    }
    if (!g_conditionStateLoaded) {
        return ConditionObservationResult::ConditionStateUnavailable;
    }
    if (g_conditionStateSavePending) {
        return ConditionObservationResult::BlockedByPendingConditionStateSave;
    }
    if (observedState == ObservedConditionState::Unknown) {
        tracker.pendingState_ = ObservedConditionState::Unknown;
        tracker.pendingStartedMs_ = 0;
        return ConditionObservationResult::ObservationUnknown;
    }

    const uint32_t conditionBit = appEventsConditionBit(tracker.conditionId_);
    const bool confirmedActive = (g_activeConditionIdBits & conditionBit) != 0;
    const bool observedActive = observedState == ObservedConditionState::Active;
    if (confirmedActive == observedActive) {
        tracker.pendingState_ = ObservedConditionState::Unknown;
        tracker.pendingStartedMs_ = 0;
        return ConditionObservationResult::ConditionUnchanged;
    }
    if (!appEventsValidInput(transitionEvent)) {
        return ConditionObservationResult::InvalidArgument;
    }
    if (!g_appEventStore.isReady() || !g_appEventStore.isWritable()) {
        return ConditionObservationResult::EventStoreUnavailable;
    }

    const uint32_t confirmationMs = observedActive
                                        ? tracker.activationConfirmationMs_
                                        : tracker.recoveryConfirmationMs_;
    const uint32_t nowMs = millis();
    if (tracker.pendingState_ != observedState) {
        tracker.pendingState_ = observedState;
        tracker.pendingStartedMs_ = nowMs;
        if (confirmationMs != 0) {
            return observedActive
                       ? ConditionObservationResult::ActivationConfirmationPending
                       : ConditionObservationResult::RecoveryConfirmationPending;
        }
    } else if (static_cast<uint32_t>(nowMs - tracker.pendingStartedMs_) < confirmationMs) {
        return observedActive
                   ? ConditionObservationResult::ActivationConfirmationPending
                   : ConditionObservationResult::RecoveryConfirmationPending;
    }

    const EventKind eventKind = observedActive
                                    ? EventKind::ConditionActivated
                                    : EventKind::ConditionRecovered;
    const DiscreteEventAppendResult appendResult =
        appEventsAppend(transitionEvent, eventKind, tracker.conditionId_);
    if (appendResult == DiscreteEventAppendResult::EventStoreUnavailable) {
        tracker.pendingState_ = ObservedConditionState::Unknown;
        tracker.pendingStartedMs_ = 0;
        return ConditionObservationResult::EventStoreUnavailable;
    }
    if (appendResult != DiscreteEventAppendResult::Stored) {
        tracker.pendingState_ = ObservedConditionState::Unknown;
        tracker.pendingStartedMs_ = 0;
        return appendResult == DiscreteEventAppendResult::InvalidEvent
                   ? ConditionObservationResult::InvalidArgument
                   : ConditionObservationResult::EventStoreWriteFailed;
    }

    if (observedActive) {
        g_activeConditionIdBits |= conditionBit;
    } else {
        g_activeConditionIdBits &= ~conditionBit;
    }
    tracker.pendingState_ = ObservedConditionState::Unknown;
    tracker.pendingStartedMs_ = 0;
    if (!appEventsSaveConditionState()) {
        return ConditionObservationResult::EventStoredButConditionStateSaveFailed;
    }
    return observedActive
               ? ConditionObservationResult::ActivationEventStored
               : ConditionObservationResult::RecoveryEventStored;
}

bool Esp32BaseAppEvents::forgetConditionState(uint8_t conditionId) {
    if (conditionId < MIN_CONDITION_ID || conditionId > MAX_CONDITION_ID ||
        !g_conditionStateLoaded || g_conditionStateSavePending) {
        return false;
    }
    const uint32_t bit = appEventsConditionBit(conditionId);
    if ((g_activeConditionIdBits & bit) == 0) {
        appEventsAdvanceConditionRevision();
        return true;
    }
    const uint32_t previousBits = g_activeConditionIdBits;
    g_activeConditionIdBits &= ~bit;
    if (!appEventsSaveConditionState()) {
        g_activeConditionIdBits = previousBits;
        g_conditionStateSavePending = false;
        return false;
    }
    appEventsAdvanceConditionRevision();
    return true;
}

bool Esp32BaseAppEvents::forgetAllConditionStates() {
    if (!g_conditionStateLoaded || g_conditionStateSavePending) {
        return false;
    }
    if (g_activeConditionIdBits == 0) {
        appEventsAdvanceConditionRevision();
        return true;
    }
    const uint32_t previousBits = g_activeConditionIdBits;
    g_activeConditionIdBits = 0;
    if (!appEventsSaveConditionState()) {
        g_activeConditionIdBits = previousBits;
        g_conditionStateSavePending = false;
        return false;
    }
    appEventsAdvanceConditionRevision();
    return true;
}
#endif

bool Esp32BaseAppEvents::readLatest(uint32_t offset,
                                    uint32_t limit,
                                    ReadCallback callback,
                                    void* user) {
    if (!callback || limit == 0) return false;
    AppEventsStatus status;
    if (!readStatus(status) || !status.eventStore.ready) return false;
    if (status.eventStore.recordCount == 0) return true;
    uint8_t scratch[kAppEventPayloadBytes];
    AppEventsReadContext context = {callback, user, offset, limit, 0, 0};
    return g_appEventStore.readLatest(0,
                                      status.eventStore.recordCount,
                                      scratch,
                                      sizeof(scratch),
                                      appEventsReadAdapter,
                                      &context);
}

Esp32BaseAppEvents::EventReadResult Esp32BaseAppEvents::readById(uint32_t recordId,
                                                                 EventRecord& event) {
    uint8_t payload[kAppEventPayloadBytes];
    Esp32BaseRecordStore::RecordMetadata record;
    const Esp32BaseRecordStore::RecordReadResult result =
        g_appEventStore.readById(recordId, payload, sizeof(payload), record);
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

bool Esp32BaseAppEvents::clearEventHistory() { return g_appEventStore.clear(); }

bool Esp32BaseAppEvents::readStatus(AppEventsStatus& status) {
    const bool ok = g_appEventStore.readStatus(status.eventStore);
#if ESP32BASE_ENABLE_APP_EVENT_CONDITIONS
    status.conditionStateLoaded = g_conditionStateLoaded;
    status.conditionStateSavePending = g_conditionStateSavePending;
    status.activeConditionCount = appEventsCountBits(g_activeConditionIdBits);
#endif
    return ok;
}

bool Esp32BaseAppEvents::isEventStoreReady() { return g_appEventStore.isReady(); }
bool Esp32BaseAppEvents::isEventStoreWritable() { return g_appEventStore.isWritable(); }

uint32_t Esp32BaseAppEvents::eventCount() {
    AppEventsStatus status;
    return readStatus(status) ? status.eventStore.recordCount : 0;
}

uint32_t Esp32BaseAppEvents::eventCapacity() {
    AppEventsStatus status;
    return readStatus(status) ? status.eventStore.capacity : 0;
}

const char* Esp32BaseAppEvents::eventStorePath() { return g_appEventStore.path(); }

const char* Esp32BaseAppEvents::lastErrorReason() {
#if ESP32BASE_ENABLE_APP_EVENT_CONDITIONS
    if (g_conditionStateError[0] != '\0') {
        return g_conditionStateError;
    }
#endif
    return g_appEventStore.lastErrorReason();
}

const char* Esp32BaseAppEvents::levelName(Level level) {
    switch (level) {
        case Level::Info: return "info";
        case Level::Warning: return "warning";
        case Level::Error: return "error";
        default: return "unknown";
    }
}

const char* Esp32BaseAppEvents::eventKindName(EventKind eventKind) {
    switch (eventKind) {
        case EventKind::Discrete: return "discrete";
        case EventKind::ConditionActivated: return "condition_activated";
        case EventKind::ConditionRecovered: return "condition_recovered";
        default: return "unknown";
    }
}

#endif
