#pragma once

#include <Arduino.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>

#include "../Esp32BaseProfile.h"
#include "Esp32BaseRecordStore.h"

class Esp32BaseAppEvents {
public:
    enum class Level : uint8_t {
        Info = 1,
        Warning = 2,
        Error = 3
    };

    enum class EventKind : uint8_t {
        Discrete = 1,
        ConditionActivated = 2,
        ConditionRecovered = 3
    };

    struct EventInput {
        uint32_t eventCode = 0;
        uint32_t reasonCode = 0;
        uint32_t objectId = 0;
        int32_t value1 = 0;
        int32_t value2 = 0;
        uint8_t flags = 0;
        Level level = Level::Info;
    };

    struct EventRecord {
        uint32_t recordId = 0;
        Esp32BaseRecordStore::RecordTiming timing;
        uint32_t eventCode = 0;
        uint32_t reasonCode = 0;
        uint32_t objectId = 0;
        int32_t value1 = 0;
        int32_t value2 = 0;
        uint8_t flags = 0;
        Level level = Level::Info;
        EventKind eventKind = EventKind::Discrete;
        uint8_t conditionId = 0;
    };

    struct AppEventsStatus {
        Esp32BaseRecordStore::StoreStatus eventStore;
#if ESP32BASE_ENABLE_APP_EVENT_CONDITIONS
        bool conditionStateLoaded = false;
        bool conditionStateSavePending = false;
        uint8_t activeConditionCount = 0;
#endif
    };

    enum class EventReadResult : uint8_t {
        Found,
        NotFound,
        Corrupt,
        IoError,
        InvalidArgument
    };

    enum class DiscreteEventAppendResult : uint8_t {
        Stored,
        InvalidEvent,
        EventStoreUnavailable,
        EventStoreWriteFailed
    };

#if ESP32BASE_ENABLE_APP_EVENT_CONDITIONS
    static constexpr uint8_t MIN_CONDITION_ID = 1;
    static constexpr uint8_t MAX_CONDITION_ID = 32;
    static constexpr uint32_t MAX_CONFIRMATION_MS = static_cast<uint32_t>(INT32_MAX);

    enum class ObservedConditionState : uint8_t {
        Inactive,
        Active,
        Unknown
    };

    enum class ConditionObservationResult : uint8_t {
        ConditionUnchanged,
        ActivationConfirmationPending,
        RecoveryConfirmationPending,
        ActivationEventStored,
        RecoveryEventStored,
        ObservationUnknown,
        InvalidArgument,
        EventStoreUnavailable,
        EventStoreWriteFailed,
        ConditionStateUnavailable,
        EventStoredButConditionStateSaveFailed,
        BlockedByPendingConditionStateSave
    };

    class ConditionStateTracker {
    public:
        ConditionStateTracker(uint8_t conditionId,
                              uint32_t activationConfirmationMs,
                              uint32_t recoveryConfirmationMs);
        ConditionStateTracker(const ConditionStateTracker&) = delete;
        ConditionStateTracker& operator=(const ConditionStateTracker&) = delete;

        uint8_t conditionId() const;

    private:
        friend class Esp32BaseAppEvents;

        uint32_t activationConfirmationMs_;
        uint32_t recoveryConfirmationMs_;
        uint32_t pendingStartedMs_;
        uint32_t stateRevision_;
        uint8_t conditionId_;
        ObservedConditionState pendingState_;
        bool registered_;
    };
#endif

    using ReadCallback = void (*)(const EventRecord& event, void* user);

    static bool begin();
    static bool reload();
    static DiscreteEventAppendResult appendDiscreteEvent(const EventInput& event);
#if ESP32BASE_ENABLE_APP_EVENT_CONDITIONS
    static ConditionObservationResult observeConditionState(ConditionStateTracker& tracker,
                                                             ObservedConditionState observedState,
                                                             const EventInput& transitionEvent);
    static bool forgetConditionState(uint8_t conditionId);
    static bool forgetAllConditionStates();
#endif
    static bool readLatest(uint32_t offset, uint32_t limit, ReadCallback callback, void* user = nullptr);
    static EventReadResult readById(uint32_t recordId, EventRecord& event);
    static bool clearEventHistory();
    static bool readStatus(AppEventsStatus& status);
    static bool isEventStoreReady();
    static bool isEventStoreWritable();
    static uint32_t eventCount();
    static uint32_t eventCapacity();
    static const char* eventStorePath();
    static const char* lastErrorReason();
    static const char* levelName(Level level);
    static const char* eventKindName(EventKind eventKind);
};
