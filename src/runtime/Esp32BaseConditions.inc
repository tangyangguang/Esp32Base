#include "../Esp32BaseProfile.h"

#if ESP32BASE_ENABLE_CONDITIONS

#include "Esp32BaseConditions.h"

#include <string.h>

namespace {

uint32_t g_activeConditionBits = 0;
uint32_t g_registeredConditionBits = 0;
uint32_t g_knownConditionBits = 0;
uint32_t g_conditionStateRevision = 1;
bool g_conditionStateLoaded = false;
char g_conditionError[64] = "";

uint32_t conditionBit(uint8_t conditionId) {
    return 1UL << (conditionId - 1U);
}

uint8_t countConditionBits(uint32_t bits) {
    uint8_t count = 0;
    while (bits != 0) {
        bits &= bits - 1U;
        ++count;
    }
    return count;
}

void setConditionError(const char* reason) {
    strlcpy(g_conditionError, reason ? reason : "", sizeof(g_conditionError));
}

void advanceConditionRevision() {
    ++g_conditionStateRevision;
    if (g_conditionStateRevision == 0) {
        g_conditionStateRevision = 1;
    }
    g_registeredConditionBits = 0;
}

} // namespace

Esp32BaseConditions::ConditionTracker::ConditionTracker(
    uint8_t conditionId,
    uint32_t activationConfirmationMs,
    uint32_t recoveryConfirmationMs)
    : activationConfirmationMs_(activationConfirmationMs),
      recoveryConfirmationMs_(recoveryConfirmationMs),
      pendingStartedMs_(0),
      stateRevision_(0),
      conditionId_(conditionId),
      pendingState_(ObservedState::Unknown),
      registered_(false) {}

uint8_t Esp32BaseConditions::ConditionTracker::conditionId() const {
    return conditionId_;
}

bool Esp32BaseConditions::begin() {
    g_activeConditionBits = 0;
    g_knownConditionBits = 0;
    g_conditionStateLoaded = true;
    setConditionError("");
    advanceConditionRevision();
    return true;
}

bool Esp32BaseConditions::reload() {
    return begin();
}

Esp32BaseConditions::ObservationResult Esp32BaseConditions::observe(
    ConditionTracker& tracker,
    ObservedState observedState) {
    if (tracker.conditionId_ < MIN_CONDITION_ID || tracker.conditionId_ > MAX_CONDITION_ID ||
        tracker.activationConfirmationMs_ > MAX_CONFIRMATION_MS ||
        tracker.recoveryConfirmationMs_ > MAX_CONFIRMATION_MS ||
        (observedState != ObservedState::Inactive &&
         observedState != ObservedState::Active &&
         observedState != ObservedState::Unknown)) {
        return ObservationResult::InvalidArgument;
    }
    if (tracker.stateRevision_ != g_conditionStateRevision) {
        tracker.pendingState_ = ObservedState::Unknown;
        tracker.pendingStartedMs_ = 0;
        tracker.registered_ = false;
        tracker.stateRevision_ = g_conditionStateRevision;
    }
    if (!tracker.registered_) {
        const uint32_t bit = conditionBit(tracker.conditionId_);
        if ((g_registeredConditionBits & bit) != 0) {
            return ObservationResult::InvalidArgument;
        }
        g_registeredConditionBits |= bit;
        tracker.registered_ = true;
    }
    if (!g_conditionStateLoaded) {
        return ObservationResult::StateUnavailable;
    }
    if (observedState == ObservedState::Unknown) {
        g_knownConditionBits &= ~conditionBit(tracker.conditionId_);
        tracker.pendingState_ = ObservedState::Unknown;
        tracker.pendingStartedMs_ = 0;
        return ObservationResult::ObservationUnknown;
    }

    const uint32_t bit = conditionBit(tracker.conditionId_);
    const bool confirmedActive = (g_activeConditionBits & bit) != 0;
    const bool observedActive = observedState == ObservedState::Active;
    if ((g_knownConditionBits & bit) && confirmedActive == observedActive) {
        tracker.pendingState_ = ObservedState::Unknown;
        tracker.pendingStartedMs_ = 0;
        return ObservationResult::ConditionUnchanged;
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
                       ? ObservationResult::ActivationConfirmationPending
                       : ObservationResult::RecoveryConfirmationPending;
        }
    } else if (static_cast<uint32_t>(nowMs - tracker.pendingStartedMs_) < confirmationMs) {
        return observedActive
                   ? ObservationResult::ActivationConfirmationPending
                   : ObservationResult::RecoveryConfirmationPending;
    }

    const uint32_t nextBits = observedActive
                                  ? (g_activeConditionBits | bit)
                                  : (g_activeConditionBits & ~bit);
    const bool wasKnown = (g_knownConditionBits & bit) != 0;
    g_activeConditionBits = nextBits;
    g_knownConditionBits |= bit;
    tracker.pendingState_ = ObservedState::Unknown;
    tracker.pendingStartedMs_ = 0;
    return observedActive ? ObservationResult::Activated
                          : wasKnown ? ObservationResult::Recovered : ObservationResult::ConditionUnchanged;
}

bool Esp32BaseConditions::isActive(uint8_t conditionId, bool& active) {
    active = false;
    if (!g_conditionStateLoaded || conditionId < MIN_CONDITION_ID ||
        conditionId > MAX_CONDITION_ID || !(g_knownConditionBits & conditionBit(conditionId))) {
        return false;
    }
    active = (g_activeConditionBits & conditionBit(conditionId)) != 0;
    return true;
}

bool Esp32BaseConditions::forget(uint8_t conditionId) {
    if (!g_conditionStateLoaded || conditionId < MIN_CONDITION_ID ||
        conditionId > MAX_CONDITION_ID) {
        return false;
    }
    const uint32_t nextBits = g_activeConditionBits & ~conditionBit(conditionId);
    g_activeConditionBits = nextBits;
    g_knownConditionBits &= ~conditionBit(conditionId);
    advanceConditionRevision();
    return true;
}

bool Esp32BaseConditions::forgetAll() {
    if (!g_conditionStateLoaded) {
        return false;
    }
    g_activeConditionBits = 0;
    g_knownConditionBits = 0;
    advanceConditionRevision();
    return true;
}

bool Esp32BaseConditions::readStatus(ConditionsStatus& status) {
    status.stateLoaded = g_conditionStateLoaded;
    status.activeConditionCount = countConditionBits(g_activeConditionBits & g_knownConditionBits);
    status.errorReason = g_conditionError[0] ? g_conditionError : "none";
    return g_conditionStateLoaded;
}

const char* Esp32BaseConditions::lastErrorReason() {
    return g_conditionError[0] ? g_conditionError : "none";
}

#endif
