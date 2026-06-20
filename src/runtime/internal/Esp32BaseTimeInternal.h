#pragma once

#include <stdint.h>

namespace esp32base_internal {

bool timeAcceptRtcEpoch(uint32_t epochSec);
bool timeAcceptNtpEpoch(uint32_t epochSec);

#if defined(ESP32BASE_TIME_NATIVE_TEST)
void timeNativeReset();
uint32_t timeNativeLastSystemEpoch();
#endif

}
