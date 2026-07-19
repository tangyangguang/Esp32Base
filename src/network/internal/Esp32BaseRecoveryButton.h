#pragma once

#include <stdint.h>

namespace esp32base_internal {

class RecoveryButtonTracker {
public:
    void reset(uint32_t nowMs) {
        _rawPressed = false;
        _stablePressed = false;
        _triggeredThisPress = false;
        _rawChangedMs = nowMs;
        _pressedMs = 0;
    }

    bool update(bool rawPressed, uint32_t nowMs, uint32_t debounceMs, uint32_t holdMs) {
        if (rawPressed != _rawPressed) {
            _rawPressed = rawPressed;
            _rawChangedMs = nowMs;
        }
        if (rawPressed != _stablePressed &&
            static_cast<uint32_t>(nowMs - _rawChangedMs) >= debounceMs) {
            _stablePressed = rawPressed;
            _triggeredThisPress = false;
            if (rawPressed) {
                _pressedMs = nowMs;
            }
        }
        if (!_stablePressed || _triggeredThisPress ||
            static_cast<uint32_t>(nowMs - _pressedMs) < holdMs) {
            return false;
        }
        _triggeredThisPress = true;
        return true;
    }

private:
    bool _rawPressed = false;
    bool _stablePressed = false;
    bool _triggeredThisPress = false;
    uint32_t _rawChangedMs = 0;
    uint32_t _pressedMs = 0;
};

}  // namespace esp32base_internal
