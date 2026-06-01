#pragma once

#include "../../Esp32BaseProfile.h"

#include <Arduino.h>

#if ESP32BASE_ENABLE_WATCHDOG
#include "../Esp32BaseWatchdog.h"
#endif

namespace Esp32BaseLongOperation {

class LongOperationScope {
public:
    LongOperationScope() {
#if ESP32BASE_ENABLE_WATCHDOG
        active_ = Esp32BaseWatchdog::enterLongOperation();
#endif
    }

    ~LongOperationScope() {
#if ESP32BASE_ENABLE_WATCHDOG
        if (active_) {
            Esp32BaseWatchdog::exitLongOperation();
        }
#endif
    }

    LongOperationScope(const LongOperationScope&) = delete;
    LongOperationScope& operator=(const LongOperationScope&) = delete;

private:
    bool active_ = false;
};

static void service() {
#if ESP32BASE_ENABLE_WATCHDOG
    Esp32BaseWatchdog::feed();
#endif
    yield();
}

}
