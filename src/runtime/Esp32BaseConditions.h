#pragma once

#include <Arduino.h>
#include <limits.h>
#include <stdint.h>

class Esp32BaseConditions {
public:
    static constexpr uint8_t MIN_CONDITION_ID = 1;
    static constexpr uint8_t MAX_CONDITION_ID = 32;
    static constexpr uint32_t MAX_CONFIRMATION_MS = static_cast<uint32_t>(INT32_MAX);

    enum class ObservedState : uint8_t {
        Inactive,
        Active,
        Unknown
    };

    enum class ObservationResult : uint8_t {
        ConditionUnchanged,
        ActivationConfirmationPending,
        RecoveryConfirmationPending,
        Activated,
        Recovered,
        ObservationUnknown,
        InvalidArgument,
        StateUnavailable,
        StateWriteFailed
    };

    struct ConditionsStatus {
        bool stateLoaded = false;
        uint8_t activeConditionCount = 0;
        const char* errorReason = nullptr;
    };

    class ConditionTracker {
    public:
        ConditionTracker(uint8_t conditionId,
                         uint32_t activationConfirmationMs,
                         uint32_t recoveryConfirmationMs);
        ConditionTracker(const ConditionTracker&) = delete;
        ConditionTracker& operator=(const ConditionTracker&) = delete;

        uint8_t conditionId() const;

    private:
        friend class Esp32BaseConditions;

        uint32_t activationConfirmationMs_;
        uint32_t recoveryConfirmationMs_;
        uint32_t pendingStartedMs_;
        uint32_t stateRevision_;
        uint8_t conditionId_;
        ObservedState pendingState_;
        bool registered_;
    };

    static bool begin();
    static bool reload();
    static ObservationResult observe(ConditionTracker& tracker, ObservedState observedState);
    static bool isActive(uint8_t conditionId, bool& active);
    static bool forget(uint8_t conditionId);
    static bool forgetAll();
    static bool readStatus(ConditionsStatus& status);
    static const char* lastErrorReason();
};
