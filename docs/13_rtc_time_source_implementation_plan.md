# RTC Time Source Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add optional DS3231/PCF8563 RTC support as a trusted time source without turning Esp32Base into a full RTC peripheral framework.

**Architecture:** Add `Esp32BaseTime` as the common source of truth for real time and boot time mapping, then add `Esp32BaseRtc` as an optional I2C RTC backend selected by compile-time configuration. Existing NTP, App Events, logs, Web Status, and event resolution move to `Esp32BaseTime` so the common logic stays `NTP > RTC > uptime`.

**Tech Stack:** Arduino ESP32 Core, PlatformIO, Unity native harness, ESP-IDF time primitives, Arduino `Wire` / `TwoWire`, existing `.h` + `.inc` Esp32Base module pattern.

---

## File Structure

- Create: `src/runtime/Esp32BaseTime.h`
  Public time facade: `Snapshot`, source enum, current time, formatting, callback, current-boot event resolution.

- Create: `src/runtime/internal/Esp32BaseTimeInternal.h`
  Internal methods used by NTP and RTC: accept trusted epochs, native-test reset hook, and uptime injection where needed.

- Create: `src/runtime/Esp32BaseTime.inc`
  Implementation of boot session, source priority, log timestamp provider, system time update, and callback dispatch.

- Create: `src/runtime/Esp32BaseRtc.h`
  Public RTC facade: driver/status enums, `configure`, `begin`, `refresh`, `readEpoch`, `setEpoch`, cached status, driver name.

- Create: `src/runtime/Esp32BaseRtc.inc`
  Common RTC implementation, selected driver binding, I2C ownership, write-back threshold handling.

- Create: `src/runtime/internal/Esp32BaseRtcDrivers.h`
  Internal driver contract and chip-specific helpers shared by tests and `Esp32BaseRtc.inc`.

- Create: `src/runtime/internal/Esp32BaseRtcInternal.h`
  Internal RTC write-back hook used by NTP integration without exposing it as public API.

- Create: `src/runtime/internal/Esp32BaseRtcCalendar.h`
  Shared UTC calendar conversion and BCD helpers used by both RTC drivers and native tests.

- Create: `src/runtime/internal/Esp32BaseRtcBus.h`
  Shared bounded I2C register read/write helpers used by both RTC drivers.

- Create: `src/runtime/internal/Esp32BaseRtcDs3231.inc`
  DS3231 driver implementation only: calendar register read/write, OSF mapping, status bit preservation.

- Create: `src/runtime/internal/Esp32BaseRtcPcf8563.inc`
  PCF8563 driver implementation only: BCD calendar read/write, VL/clock integrity mapping, status bit preservation.

- Create: `test/test_native_time_harness/test_main.cpp`
  Native Unity tests for `Esp32BaseTime`, RTC conversion, driver behavior, source priority, and custom bus/address.

- Create: `test/test_native_time_harness/stubs/Arduino.h`
  Native Arduino stub with `millis`, `strlcpy`, `Serial`, and `TwoWire` include path support.

- Create: `test/test_native_time_harness/stubs/Wire.h`
  Native `TwoWire` / `Wire` fake used by DS3231 and PCF8563 register tests.

- Create: `test/test_native_time_harness/stubs/esp_timer.h`
  Native `esp_timer_get_time()` stub driven by test-controlled uptime.

- Create: `examples/rtc_time_source/src/main.cpp`
  Minimal RTC example showing DS3231 and PCF8563 compile-time selection and `Wire.begin()` ownership.

- Create: `examples/rtc_time_source/platformio.ini`
  Example envs: `esp32_ds3231`, `esp32_pcf8563`, and one RTC-missing diagnostic build.

- Modify: `platformio.ini`
  Add `native_time_harness`.

- Modify: `src/Esp32BaseProfile.h`
  Add `ESP32BASE_ENABLE_TIME`, `ESP32BASE_ENABLE_RTC`, RTC driver constants, and validation.

- Modify: `src/Esp32Base.h`
  Include `Esp32BaseTime.h` when `ESP32BASE_ENABLE_TIME`, include `Esp32BaseRtc.h` when `ESP32BASE_ENABLE_RTC`.

- Modify: `src/Esp32Base.cpp`
  Initialize Time and RTC, replace App Events NTP time provider, call RTC handle, include new `.inc` files.

- Modify: `src/network/Esp32BaseNtp.h` and `src/network/Esp32BaseNtp.inc`
  Route trusted NTP sync into Time, keep `snapshot`, `isRealTime`, `formatTime`, and `resolveCurrentBootEvent` delegating to Time, trigger RTC write-back.

- Modify: `src/runtime/Esp32BaseAppEventLog.h` and `src/runtime/Esp32BaseAppEventLog.inc`
  Keep storage format stable; update comments so `FLAG_TIME_SYNCED` means trusted real time from RTC or NTP.

- Modify: `src/web/internal/WebContext.h`, `src/web/internal/WebStatus.cpp`, `src/web/internal/WebAppEvents.cpp`, `src/web/internal/WebFs.cpp`, `src/web/internal/WebInternal.h`
  Read time from `Esp32BaseTime`, show source and RTC status, remove NTP-only assumptions.

- Modify: `README.md`, `docs/01_architecture.md`, `docs/02_profiles.md`, `docs/03_api.md`, `docs/04_web.md`, `docs/07_diagnostics.md`, `docs/08_arduino_core_compat.md`, `docs/10_known_limitations.md`
  Document application usage, configuration, boundaries, and diagnostics.

---

### Task 1: Native Time Harness Skeleton

**Files:**
- Modify: `platformio.ini`
- Create: `test/test_native_time_harness/test_main.cpp`
- Create: `test/test_native_time_harness/stubs/Arduino.h`
- Create: `test/test_native_time_harness/stubs/esp_timer.h`

- [ ] **Step 1: Add the native time harness env**

Modify `platformio.ini`:

```ini
[env:native_time_harness]
platform = native
test_framework = unity
test_build_src = yes
build_flags =
  -D ESP32BASE_TIME_NATIVE_TEST=1
  -D ESP32BASE_LOG_LEVEL=ESP32BASE_LOG_NONE
  -D ESP32BASE_ENABLE_TIME=1
  -I test/test_native_time_harness/stubs
build_src_filter =
  -<*>
  +<core/Esp32BaseLog.cpp>
test_filter = test_native_time_harness
```

- [ ] **Step 2: Add native Arduino and timer stubs**

Create `test/test_native_time_harness/stubs/Arduino.h`:

```cpp
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

extern uint32_t g_nativeMillis;

inline uint32_t millis() {
    return g_nativeMillis;
}

inline size_t strlcpy(char* dst, const char* src, size_t size) {
    const size_t srcLen = src ? std::strlen(src) : 0;
    if (size > 0) {
        const size_t copyLen = srcLen >= size ? size - 1 : srcLen;
        if (copyLen > 0 && src) {
            std::memcpy(dst, src, copyLen);
        }
        dst[copyLen] = '\0';
    }
    return srcLen;
}

struct NativeSerial {
    void begin(uint32_t) {}
    void println(const char*) {}
};

extern NativeSerial Serial;
```

Create `test/test_native_time_harness/stubs/esp_timer.h`:

```cpp
#pragma once

#include <cstdint>

extern int64_t g_nativeEspTimerUs;

inline int64_t esp_timer_get_time() {
    return g_nativeEspTimerUs;
}
```

- [ ] **Step 3: Write the first failing Time test**

Create `test/test_native_time_harness/test_main.cpp`:

```cpp
#include <unity.h>

#include "runtime/Esp32BaseTime.h"
#include "runtime/internal/Esp32BaseTimeInternal.h"
#include "runtime/Esp32BaseTime.inc"

uint32_t g_nativeMillis = 0;
int64_t g_nativeEspTimerUs = 0;
NativeSerial Serial;

static void resetTimeHarness() {
    g_nativeMillis = 0;
    g_nativeEspTimerUs = 0;
    esp32base_internal::timeNativeReset();
}

void test_time_defaults_to_uptime_without_real_time() {
    resetTimeHarness();
    g_nativeEspTimerUs = 12LL * 1000000LL;

    TEST_ASSERT_TRUE(Esp32BaseTime::initBootSession());
    const Esp32BaseTime::Snapshot snap = Esp32BaseTime::snapshot();

    TEST_ASSERT_FALSE(snap.synced);
    TEST_ASSERT_EQUAL(Esp32BaseTime::SOURCE_UPTIME, snap.source);
    TEST_ASSERT_EQUAL_UINT32(0, snap.epochSec);
    TEST_ASSERT_EQUAL_UINT32(12, snap.uptimeSec);
    TEST_ASSERT_TRUE(snap.bootId > 0);
    TEST_ASSERT_EQUAL_UINT32(0, snap.bootStartEpochSec);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_time_defaults_to_uptime_without_real_time);
    return UNITY_END();
}
```

- [ ] **Step 4: Run the failing harness**

Run:

```bash
pio test -e native_time_harness
```

Expected: compile fails because `runtime/Esp32BaseTime.h` does not exist.

- [ ] **Step 5: Commit the test harness skeleton**

```bash
git add platformio.ini test/test_native_time_harness
git commit -m "test: add native time harness"
```

---

### Task 2: `Esp32BaseTime` Core Behavior

**Files:**
- Create: `src/runtime/Esp32BaseTime.h`
- Create: `src/runtime/internal/Esp32BaseTimeInternal.h`
- Create: `src/runtime/Esp32BaseTime.inc`
- Modify: `src/Esp32BaseProfile.h`
- Modify: `src/Esp32Base.h`
- Modify: `src/Esp32Base.cpp`
- Modify: `test/test_native_time_harness/test_main.cpp`

- [ ] **Step 1: Add failing tests for trusted time priority and resolution**

Append to `test/test_native_time_harness/test_main.cpp`:

```cpp
void test_rtc_time_establishes_boot_mapping() {
    resetTimeHarness();
    g_nativeEspTimerUs = 5LL * 1000000LL;

    TEST_ASSERT_TRUE(Esp32BaseTime::initBootSession());
    TEST_ASSERT_TRUE(esp32base_internal::timeAcceptRtcEpoch(1700000105UL));

    const Esp32BaseTime::Snapshot snap = Esp32BaseTime::snapshot();
    TEST_ASSERT_TRUE(snap.synced);
    TEST_ASSERT_EQUAL(Esp32BaseTime::SOURCE_RTC, snap.source);
    TEST_ASSERT_EQUAL_UINT32(1700000105UL, snap.epochSec);
    TEST_ASSERT_EQUAL_UINT32(5, snap.uptimeSec);
    TEST_ASSERT_EQUAL_UINT32(1700000100UL, snap.bootStartEpochSec);
    TEST_ASSERT_EQUAL_UINT32(1700000105UL, esp32base_internal::timeNativeLastSystemEpoch());
}

void test_ntp_overrides_rtc_time() {
    resetTimeHarness();
    g_nativeEspTimerUs = 10LL * 1000000LL;

    TEST_ASSERT_TRUE(Esp32BaseTime::initBootSession());
    TEST_ASSERT_TRUE(esp32base_internal::timeAcceptRtcEpoch(1700000110UL));
    TEST_ASSERT_TRUE(esp32base_internal::timeAcceptNtpEpoch(1700000210UL));

    const Esp32BaseTime::Snapshot snap = Esp32BaseTime::snapshot();
    TEST_ASSERT_TRUE(snap.synced);
    TEST_ASSERT_EQUAL(Esp32BaseTime::SOURCE_NTP, snap.source);
    TEST_ASSERT_EQUAL_UINT32(1700000210UL, snap.epochSec);
    TEST_ASSERT_EQUAL_UINT32(1700000200UL, snap.bootStartEpochSec);
    TEST_ASSERT_EQUAL_UINT32(1700000210UL, esp32base_internal::timeNativeLastSystemEpoch());
}

void test_resolve_current_boot_event_uses_active_mapping() {
    resetTimeHarness();
    g_nativeEspTimerUs = 30LL * 1000000LL;

    TEST_ASSERT_TRUE(Esp32BaseTime::initBootSession());
    const uint32_t bootId = Esp32BaseTime::snapshot().bootId;
    TEST_ASSERT_TRUE(esp32base_internal::timeAcceptRtcEpoch(1700000030UL));

    uint32_t resolved = 0;
    TEST_ASSERT_TRUE(Esp32BaseTime::resolveCurrentBootEvent(bootId, 7, &resolved));
    TEST_ASSERT_EQUAL_UINT32(1700000007UL, resolved);
}
```

Add these to `main()`:

```cpp
RUN_TEST(test_rtc_time_establishes_boot_mapping);
RUN_TEST(test_ntp_overrides_rtc_time);
RUN_TEST(test_resolve_current_boot_event_uses_active_mapping);
```

- [ ] **Step 2: Run tests and verify they fail**

Run:

```bash
pio test -e native_time_harness
```

Expected: compile fails because `Esp32BaseTime` and internal accept functions are missing.

- [ ] **Step 3: Add profile and public includes**

Modify `src/Esp32BaseProfile.h` after the `ESP32BASE_ENABLE_NTP` default block:

```cpp
#ifndef ESP32BASE_ENABLE_RTC
#define ESP32BASE_ENABLE_RTC 0
#endif

#ifndef ESP32BASE_TIME_SYNC_MIN_EPOCH
#ifdef ESP32BASE_NTP_SYNC_MIN_EPOCH
#define ESP32BASE_TIME_SYNC_MIN_EPOCH ESP32BASE_NTP_SYNC_MIN_EPOCH
#else
#define ESP32BASE_TIME_SYNC_MIN_EPOCH 1700000000UL
#endif
#endif

#ifndef ESP32BASE_ENABLE_TIME
#if ESP32BASE_ENABLE_NTP || ESP32BASE_ENABLE_RTC || ESP32BASE_ENABLE_APP_EVENTS || ESP32BASE_ENABLE_WEB
#define ESP32BASE_ENABLE_TIME 1
#else
#define ESP32BASE_ENABLE_TIME 0
#endif
#endif
```

Modify `src/Esp32Base.h` after the FileLog include block:

```cpp
#if ESP32BASE_ENABLE_TIME
#include "runtime/Esp32BaseTime.h"
#endif
```

- [ ] **Step 4: Create the Time headers**

Create `src/runtime/Esp32BaseTime.h`:

```cpp
#pragma once

#include <Arduino.h>
#include <stdint.h>
#include <stddef.h>

class Esp32BaseTime {
public:
    enum Source : uint8_t {
        SOURCE_UPTIME = 0,
        SOURCE_RTC = 1,
        SOURCE_NTP = 2
    };

    struct Snapshot {
        bool synced;
        Source source;
        uint32_t epochSec;
        uint32_t uptimeSec;
        uint32_t bootId;
        uint32_t bootStartEpochSec;
    };

    typedef void (*TimeSyncCallback)(const Snapshot& snapshot);

    static bool initBootSession();
    static Snapshot snapshot();
    static bool isRealTime();
    static bool formatTime(char* out, size_t len, const char* fmt = nullptr);
    static bool resolveCurrentBootEvent(uint32_t bootId, uint32_t uptimeSec, uint32_t* epochSec);
    static void onTimeSynced(TimeSyncCallback callback);
    static const char* sourceName(Source source);
    static const char* logTimeString();
};
```

Create `src/runtime/internal/Esp32BaseTimeInternal.h`:

```cpp
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
```

- [ ] **Step 5: Create `Esp32BaseTime.inc`**

Create `src/runtime/Esp32BaseTime.inc`:

```cpp
#include "../Esp32BaseProfile.h"

#if ESP32BASE_ENABLE_TIME

#include "Esp32BaseTime.h"
#include "internal/Esp32BaseTimeInternal.h"
#include "../core/Esp32BaseLog.h"
#include "../core/Esp32BaseSystem.h"

#include <esp_timer.h>
#include <sys/time.h>
#include <time.h>

namespace {
bool g_bootSessionReady = false;
uint32_t g_bootId = 0;
uint32_t g_bootStartEpochSec = 0;
Esp32BaseTime::Source g_source = Esp32BaseTime::SOURCE_UPTIME;
Esp32BaseTime::TimeSyncCallback g_callback = nullptr;

uint32_t timeCurrentUptimeSec() {
    return static_cast<uint32_t>(esp_timer_get_time() / 1000000LL);
}

bool timeTrustedEpoch(uint32_t epochSec) {
    return epochSec >= ESP32BASE_TIME_SYNC_MIN_EPOCH;
}

#if defined(ESP32BASE_TIME_NATIVE_TEST)
uint32_t g_lastSystemEpoch = 0;
#endif

void timeSetSystemEpoch(uint32_t epochSec) {
#if defined(ESP32BASE_TIME_NATIVE_TEST)
    g_lastSystemEpoch = epochSec;
#else
    struct timeval tv = {};
    tv.tv_sec = static_cast<time_t>(epochSec);
    settimeofday(&tv, nullptr);
#endif
}

bool timeAcceptEpoch(Esp32BaseTime::Source source, uint32_t epochSec) {
    if (!timeTrustedEpoch(epochSec)) {
        return false;
    }
    if (source == Esp32BaseTime::SOURCE_RTC && g_source == Esp32BaseTime::SOURCE_NTP) {
        return true;
    }
    Esp32BaseTime::initBootSession();
    const uint32_t uptimeSec = timeCurrentUptimeSec();
    g_bootStartEpochSec = epochSec >= uptimeSec ? epochSec - uptimeSec : 0;
    if (g_bootStartEpochSec == 0) {
        return false;
    }
    const Esp32BaseTime::Source oldSource = g_source;
    g_source = source;
    timeSetSystemEpoch(epochSec);
    Esp32BaseLog::setTimeProvider(Esp32BaseTime::logTimeString);
    if (g_callback && oldSource != g_source) {
        g_callback(Esp32BaseTime::snapshot());
    }
    return true;
}
}

bool Esp32BaseTime::initBootSession() {
    if (g_bootSessionReady) {
        return true;
    }
#if defined(ESP32BASE_TIME_NATIVE_TEST)
    g_bootId = 1;
#else
    g_bootId = Esp32BaseSystem::bootCount();
#endif
    if (g_bootId == 0) {
        g_bootId = 1;
    }
    g_bootSessionReady = true;
    ESP32BASE_LOG_I("time", "time_boot_session boot_id=%lu source=boot_count",
                    static_cast<unsigned long>(g_bootId));
    return true;
}

Esp32BaseTime::Snapshot Esp32BaseTime::snapshot() {
    Snapshot value = {};
    value.source = g_source;
    value.uptimeSec = timeCurrentUptimeSec();
    value.bootId = g_bootId ? g_bootId : 1;
    value.bootStartEpochSec = g_bootStartEpochSec;
    value.synced = g_source != SOURCE_UPTIME && g_bootStartEpochSec != 0;
    value.epochSec = value.synced ? g_bootStartEpochSec + value.uptimeSec : 0;
    return value;
}

bool Esp32BaseTime::isRealTime() {
    return snapshot().synced;
}

bool Esp32BaseTime::formatTime(char* out, size_t len, const char* fmt) {
    if (!out || len == 0) {
        return false;
    }
    const Snapshot s = snapshot();
    if (!s.synced) {
        out[0] = '\0';
        return false;
    }
    const time_t raw = static_cast<time_t>(s.epochSec);
    struct tm tmValue;
    localtime_r(&raw, &tmValue);
    return strftime(out, len, fmt ? fmt : "%Y-%m-%d %H:%M:%S", &tmValue) > 0;
}

bool Esp32BaseTime::resolveCurrentBootEvent(uint32_t bootId, uint32_t uptimeSec, uint32_t* epochSec) {
    if (!epochSec) {
        return false;
    }
    *epochSec = 0;
    const Snapshot s = snapshot();
    if (!s.synced || bootId == 0 || bootId != s.bootId || s.bootStartEpochSec == 0) {
        return false;
    }
    *epochSec = s.bootStartEpochSec + uptimeSec;
    return timeTrustedEpoch(*epochSec);
}

void Esp32BaseTime::onTimeSynced(TimeSyncCallback callback) {
    g_callback = callback;
    if (g_callback && isRealTime()) {
        g_callback(snapshot());
    }
}

const char* Esp32BaseTime::sourceName(Source source) {
    switch (source) {
        case SOURCE_NTP: return "ntp";
        case SOURCE_RTC: return "rtc";
        case SOURCE_UPTIME:
        default: return "uptime";
    }
}

const char* Esp32BaseTime::logTimeString() {
    static char buf[24];
    if (!formatTime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S")) {
        snprintf(buf, sizeof(buf), "%lu", static_cast<unsigned long>(millis()));
    }
    return buf;
}

namespace esp32base_internal {
bool timeAcceptRtcEpoch(uint32_t epochSec) {
    return timeAcceptEpoch(Esp32BaseTime::SOURCE_RTC, epochSec);
}

bool timeAcceptNtpEpoch(uint32_t epochSec) {
    return timeAcceptEpoch(Esp32BaseTime::SOURCE_NTP, epochSec);
}

#if defined(ESP32BASE_TIME_NATIVE_TEST)
uint32_t timeNativeLastSystemEpoch() {
    return g_lastSystemEpoch;
}

void timeNativeReset() {
    g_bootSessionReady = false;
    g_bootId = 0;
    g_bootStartEpochSec = 0;
    g_source = Esp32BaseTime::SOURCE_UPTIME;
    g_callback = nullptr;
    g_lastSystemEpoch = 0;
}
#endif
}

#endif
```

- [ ] **Step 6: Include Time implementation in `Esp32Base.cpp`**

Add near other `.inc` includes at the bottom of `src/Esp32Base.cpp`:

```cpp
#if ESP32BASE_ENABLE_TIME
#include "runtime/Esp32BaseTime.inc"
#endif
```

In `Esp32Base::begin()`, replace the NTP-only boot session block:

```cpp
#if ESP32BASE_ENABLE_TIME
    Esp32BaseTime::initBootSession();
#endif
#if ESP32BASE_ENABLE_NTP
    Esp32BaseNtp::initBootSession();
#endif
```

- [ ] **Step 7: Run native time tests**

Run:

```bash
pio test -e native_time_harness
```

Expected: all native time harness tests pass.

- [ ] **Step 8: Commit Time facade**

```bash
git add src/runtime/Esp32BaseTime.h src/runtime/internal/Esp32BaseTimeInternal.h src/runtime/Esp32BaseTime.inc src/Esp32BaseProfile.h src/Esp32Base.h src/Esp32Base.cpp test/test_native_time_harness/test_main.cpp
git commit -m "feat: add trusted time facade"
```

---

### Task 3: RTC Driver Test Harness and Fake I2C

**Files:**
- Create: `test/test_native_time_harness/stubs/Wire.h`
- Modify: `test/test_native_time_harness/test_main.cpp`
- Modify: `platformio.ini`

- [ ] **Step 1: Add Wire stub include and RTC flags**

Modify `platformio.ini` `native_time_harness` flags:

```ini
build_flags =
  -D ESP32BASE_TIME_NATIVE_TEST=1
  -D ESP32BASE_RTC_NATIVE_TEST=1
  -D ESP32BASE_LOG_LEVEL=ESP32BASE_LOG_NONE
  -D ESP32BASE_ENABLE_TIME=1
  -D ESP32BASE_ENABLE_RTC=1
  -I test/test_native_time_harness/stubs
```

- [ ] **Step 2: Add a fake `TwoWire`**

Create `test/test_native_time_harness/stubs/Wire.h`:

```cpp
#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

class TwoWire {
public:
    std::map<uint8_t, std::vector<uint8_t>> devices;
    uint8_t txAddress = 0;
    std::vector<uint8_t> tx;
    std::vector<uint8_t> rx;
    size_t rxIndex = 0;
    bool failEnd = false;

    void begin() {}
    void begin(int, int, uint32_t = 100000) {}

    void beginTransmission(uint8_t address) {
        txAddress = address;
        tx.clear();
    }

    size_t write(uint8_t value) {
        tx.push_back(value);
        return 1;
    }

    uint8_t endTransmission() {
        if (failEnd || devices.find(txAddress) == devices.end()) {
            return 2;
        }
        if (tx.size() >= 2) {
            uint8_t reg = tx[0];
            std::vector<uint8_t>& mem = devices[txAddress];
            for (size_t i = 1; i < tx.size() && reg < mem.size(); ++i, ++reg) {
                mem[reg] = tx[i];
            }
        }
        return 0;
    }

    uint8_t requestFrom(uint8_t address, uint8_t count) {
        rx.clear();
        rxIndex = 0;
        auto it = devices.find(address);
        if (it == devices.end() || tx.empty()) {
            return 0;
        }
        uint8_t reg = tx[0];
        for (uint8_t i = 0; i < count && reg < it->second.size(); ++i, ++reg) {
            rx.push_back(it->second[reg]);
        }
        return static_cast<uint8_t>(rx.size());
    }

    int available() {
        return static_cast<int>(rx.size() - rxIndex);
    }

    int read() {
        if (rxIndex >= rx.size()) {
            return -1;
        }
        return rx[rxIndex++];
    }
};

extern TwoWire Wire;
```

- [ ] **Step 3: Add fake `Wire` instance to tests**

In `test/test_native_time_harness/test_main.cpp`, add:

```cpp
#include <Wire.h>

TwoWire Wire;
```

- [ ] **Step 4: Run tests and verify the harness still passes**

Run:

```bash
pio test -e native_time_harness
```

Expected: existing Time tests still pass.

- [ ] **Step 5: Commit fake I2C harness**

```bash
git add platformio.ini test/test_native_time_harness
git commit -m "test: add native rtc i2c harness"
```

---

### Task 4: RTC Common Facade and Configuration

**Files:**
- Create: `src/runtime/Esp32BaseRtc.h`
- Create: `src/runtime/Esp32BaseRtc.inc`
- Create: `src/runtime/internal/Esp32BaseRtcDrivers.h`
- Modify: `src/Esp32BaseProfile.h`
- Modify: `src/Esp32Base.h`
- Modify: `src/Esp32Base.cpp`
- Modify: `test/test_native_time_harness/test_main.cpp`

- [ ] **Step 1: Add failing RTC configuration tests**

Append to `test/test_native_time_harness/test_main.cpp`:

```cpp
#include "runtime/Esp32BaseRtc.h"
#include "runtime/Esp32BaseRtc.inc"

void test_rtc_defaults_to_driver_address_when_configured_address_zero() {
    resetTimeHarness();
    Wire.devices.clear();
    Wire.devices[0x68] = std::vector<uint8_t>(32, 0);

    TEST_ASSERT_TRUE(Esp32BaseRtc::configure(Wire, 0));
    TEST_ASSERT_FALSE(Esp32BaseRtc::isAvailable());
    TEST_ASSERT_EQUAL_STRING("ds3231", Esp32BaseRtc::driverName());
}

void test_rtc_missing_is_nonfatal_status() {
    resetTimeHarness();
    Wire.devices.clear();

    TEST_ASSERT_TRUE(Esp32BaseRtc::configure(Wire, 0x68));
    TEST_ASSERT_TRUE(Esp32BaseRtc::begin());
    TEST_ASSERT_FALSE(Esp32BaseRtc::isAvailable());
    TEST_ASSERT_EQUAL(Esp32BaseRtc::STATUS_MISSING, Esp32BaseRtc::status());
}
```

Add to `main()`:

```cpp
RUN_TEST(test_rtc_defaults_to_driver_address_when_configured_address_zero);
RUN_TEST(test_rtc_missing_is_nonfatal_status);
```

- [ ] **Step 2: Run tests and verify compile failure**

Run:

```bash
pio test -e native_time_harness
```

Expected: compile fails because `Esp32BaseRtc.h` does not exist.

- [ ] **Step 3: Add RTC profile validation**

Modify `src/Esp32BaseProfile.h`:

```cpp
#define ESP32BASE_RTC_DRIVER_DS3231 1
#define ESP32BASE_RTC_DRIVER_PCF8563 2

#ifndef ESP32BASE_RTC_DRIVER
#define ESP32BASE_RTC_DRIVER ESP32BASE_RTC_DRIVER_DS3231
#endif

#ifndef ESP32BASE_RTC_AUTO_WIRE_BEGIN
#define ESP32BASE_RTC_AUTO_WIRE_BEGIN 0
#endif
#ifndef ESP32BASE_RTC_NTP_WRITEBACK
#define ESP32BASE_RTC_NTP_WRITEBACK 1
#endif
#ifndef ESP32BASE_RTC_WRITEBACK_THRESHOLD_SEC
#define ESP32BASE_RTC_WRITEBACK_THRESHOLD_SEC 2
#endif
#ifndef ESP32BASE_RTC_STATUS_REFRESH_MS
#define ESP32BASE_RTC_STATUS_REFRESH_MS 0
#endif

#if ESP32BASE_ENABLE_RTC && \
    ESP32BASE_RTC_DRIVER != ESP32BASE_RTC_DRIVER_DS3231 && \
    ESP32BASE_RTC_DRIVER != ESP32BASE_RTC_DRIVER_PCF8563
#error "ESP32BASE_RTC_DRIVER must be ESP32BASE_RTC_DRIVER_DS3231 or ESP32BASE_RTC_DRIVER_PCF8563"
#endif
#if ESP32BASE_ENABLE_RTC && !ESP32BASE_ENABLE_TIME
#error "ESP32BASE_ENABLE_RTC requires ESP32BASE_ENABLE_TIME"
#endif
#if ESP32BASE_ENABLE_NTP && !ESP32BASE_ENABLE_TIME
#error "ESP32BASE_ENABLE_NTP requires ESP32BASE_ENABLE_TIME"
#endif
#if ESP32BASE_RTC_AUTO_WIRE_BEGIN != 0 && ESP32BASE_RTC_AUTO_WIRE_BEGIN != 1
#error "ESP32BASE_RTC_AUTO_WIRE_BEGIN must be 0 or 1"
#endif
#if ESP32BASE_RTC_NTP_WRITEBACK != 0 && ESP32BASE_RTC_NTP_WRITEBACK != 1
#error "ESP32BASE_RTC_NTP_WRITEBACK must be 0 or 1"
#endif
#if ESP32BASE_RTC_WRITEBACK_THRESHOLD_SEC < 0
#error "ESP32BASE_RTC_WRITEBACK_THRESHOLD_SEC must be >= 0"
#endif
#if ESP32BASE_RTC_STATUS_REFRESH_MS < 0
#error "ESP32BASE_RTC_STATUS_REFRESH_MS must be >= 0"
#endif
```

- [ ] **Step 4: Add RTC public header**

Create `src/runtime/Esp32BaseRtc.h`:

```cpp
#pragma once

#include <Arduino.h>
#include <stdint.h>
#include <Wire.h>

class Esp32BaseRtc {
public:
    enum Driver : uint8_t {
        DRIVER_DS3231 = 1,
        DRIVER_PCF8563 = 2
    };

    enum Status : uint8_t {
        STATUS_DISABLED = 0,
        STATUS_NOT_STARTED = 1,
        STATUS_OK = 2,
        STATUS_MISSING = 3,
        STATUS_I2C_ERROR = 4,
        STATUS_TIME_INVALID = 5,
        STATUS_CLOCK_STOPPED = 6,
        STATUS_CONFIG_ERROR = 7
    };

    static bool configure(TwoWire& wire, uint8_t address = 0);
    static bool begin();
    static void handle();
    static bool refresh();
    static bool isAvailable();
    static bool isTimeValid();
    static bool readEpoch(uint32_t* epochSec);
    static bool setEpoch(uint32_t epochSec);
    static Status status();
    static const char* statusText();
    static const char* driverName();
    static uint32_t lastEpoch();
    static uint32_t lastSyncUptimeSec();
};
```

- [ ] **Step 5: Add internal driver contract**

Create `src/runtime/internal/Esp32BaseRtcDrivers.h`:

```cpp
#pragma once

#include "../Esp32BaseRtc.h"

struct Esp32BaseRtcDriverOps {
    const char* name;
    uint8_t defaultAddress;
    bool (*probe)(TwoWire& wire, uint8_t address);
    bool (*readEpoch)(TwoWire& wire, uint8_t address, uint32_t* epoch, Esp32BaseRtc::Status* status);
    bool (*writeEpoch)(TwoWire& wire, uint8_t address, uint32_t epoch, Esp32BaseRtc::Status* status);
};
```

- [ ] **Step 6: Add common RTC implementation shell**

Create `src/runtime/Esp32BaseRtc.inc` with the common state, `configure`, status getters, and driver binding placeholder:

```cpp
#include "../Esp32BaseProfile.h"

#if ESP32BASE_ENABLE_RTC

#include "Esp32BaseRtc.h"
#include "Esp32BaseTime.h"
#include "internal/Esp32BaseTimeInternal.h"
#include "internal/Esp32BaseRtcDrivers.h"
#include "../core/Esp32BaseLog.h"

extern TwoWire Wire;

namespace {
TwoWire* g_rtcWire = &Wire;
uint8_t g_rtcAddress = 0;
Esp32BaseRtc::Status g_rtcStatus = Esp32BaseRtc::STATUS_NOT_STARTED;
uint32_t g_rtcLastEpoch = 0;
uint32_t g_rtcLastSyncUptimeSec = 0;
uint32_t g_rtcLastRefreshMs = 0;

const Esp32BaseRtcDriverOps* rtcDriverOps();

bool rtcStubProbe(TwoWire&, uint8_t) {
    return false;
}

bool rtcStubRead(TwoWire&, uint8_t, uint32_t*, Esp32BaseRtc::Status* status) {
    *status = Esp32BaseRtc::STATUS_MISSING;
    return false;
}

bool rtcStubWrite(TwoWire&, uint8_t, uint32_t, Esp32BaseRtc::Status* status) {
    *status = Esp32BaseRtc::STATUS_MISSING;
    return false;
}

const Esp32BaseRtcDriverOps kRtcMissingDs3231Ops = {
    "ds3231",
    0x68,
    rtcStubProbe,
    rtcStubRead,
    rtcStubWrite
};

const Esp32BaseRtcDriverOps kRtcMissingPcf8563Ops = {
    "pcf8563",
    0x51,
    rtcStubProbe,
    rtcStubRead,
    rtcStubWrite
};

uint32_t rtcUptimeSec() {
    return Esp32BaseTime::snapshot().uptimeSec;
}
}

bool Esp32BaseRtc::configure(TwoWire& wire, uint8_t address) {
    g_rtcWire = &wire;
    g_rtcAddress = address;
    return true;
}

bool Esp32BaseRtc::begin() {
    const Esp32BaseRtcDriverOps* ops = rtcDriverOps();
    if (!ops || !g_rtcWire) {
        g_rtcStatus = STATUS_CONFIG_ERROR;
        return true;
    }
    if (g_rtcAddress == 0) {
        g_rtcAddress = ops->defaultAddress;
    }
#if ESP32BASE_RTC_AUTO_WIRE_BEGIN
    g_rtcWire->begin();
#endif
    return refresh();
}

void Esp32BaseRtc::handle() {
#if ESP32BASE_RTC_STATUS_REFRESH_MS > 0
    const uint32_t now = millis();
    if (now - g_rtcLastRefreshMs >= ESP32BASE_RTC_STATUS_REFRESH_MS) {
        g_rtcLastRefreshMs = now;
        refresh();
    }
#endif
}

bool Esp32BaseRtc::refresh() {
    uint32_t epoch = 0;
    if (!readEpoch(&epoch)) {
        return true;
    }
    g_rtcLastEpoch = epoch;
    g_rtcLastSyncUptimeSec = rtcUptimeSec();
    esp32base_internal::timeAcceptRtcEpoch(epoch);
    return true;
}

bool Esp32BaseRtc::isAvailable() {
    return g_rtcStatus == STATUS_OK;
}

bool Esp32BaseRtc::isTimeValid() {
    return g_rtcStatus == STATUS_OK && g_rtcLastEpoch != 0;
}

bool Esp32BaseRtc::readEpoch(uint32_t* epochSec) {
    const Esp32BaseRtcDriverOps* ops = rtcDriverOps();
    if (!epochSec || !ops || !g_rtcWire) {
        g_rtcStatus = STATUS_CONFIG_ERROR;
        return false;
    }
    if (g_rtcAddress == 0) {
        g_rtcAddress = ops->defaultAddress;
    }
    Status status = STATUS_OK;
    if (!ops->readEpoch(*g_rtcWire, g_rtcAddress, epochSec, &status)) {
        g_rtcStatus = status;
        return false;
    }
    g_rtcStatus = STATUS_OK;
    return true;
}

bool Esp32BaseRtc::setEpoch(uint32_t epochSec) {
    const Esp32BaseRtcDriverOps* ops = rtcDriverOps();
    if (!ops || !g_rtcWire) {
        g_rtcStatus = STATUS_CONFIG_ERROR;
        return false;
    }
    Status status = STATUS_OK;
    const bool ok = ops->writeEpoch(*g_rtcWire, g_rtcAddress ? g_rtcAddress : ops->defaultAddress, epochSec, &status);
    g_rtcStatus = ok ? STATUS_OK : status;
    return ok;
}

Esp32BaseRtc::Status Esp32BaseRtc::status() {
    return g_rtcStatus;
}

const char* Esp32BaseRtc::statusText() {
    switch (g_rtcStatus) {
        case STATUS_OK: return "ok";
        case STATUS_MISSING: return "missing";
        case STATUS_I2C_ERROR: return "i2c_error";
        case STATUS_TIME_INVALID: return "time_invalid";
        case STATUS_CLOCK_STOPPED: return "clock_stopped";
        case STATUS_CONFIG_ERROR: return "config_error";
        case STATUS_DISABLED: return "disabled";
        case STATUS_NOT_STARTED:
        default: return "not_started";
    }
}

const char* Esp32BaseRtc::driverName() {
    const Esp32BaseRtcDriverOps* ops = rtcDriverOps();
    return ops ? ops->name : "unknown";
}

uint32_t Esp32BaseRtc::lastEpoch() {
    return g_rtcLastEpoch;
}

uint32_t Esp32BaseRtc::lastSyncUptimeSec() {
    return g_rtcLastSyncUptimeSec;
}

#endif
```

Add the initial binding at the end of the same file:

```cpp
#if ESP32BASE_ENABLE_RTC
namespace {
const Esp32BaseRtcDriverOps* rtcDriverOps() {
#if ESP32BASE_RTC_DRIVER == ESP32BASE_RTC_DRIVER_PCF8563
    return &kRtcMissingPcf8563Ops;
#else
    return &kRtcMissingDs3231Ops;
#endif
}
}
#endif
```

- [ ] **Step 7: Include RTC in facade and implementation**

Modify `src/Esp32Base.h`:

```cpp
#if ESP32BASE_ENABLE_RTC
#include "runtime/Esp32BaseRtc.h"
#endif
```

Modify `src/Esp32Base.cpp` after Time init and before App Events provider setup:

```cpp
#if ESP32BASE_ENABLE_RTC
    ESP32BASE_LOG_D("base", "module_begin name=rtc");
    {
        const bool ok = Esp32BaseRtc::begin();
        if (!optionalOk(ok, "rtc")) return false;
        if (ok) ESP32BASE_LOG_D("base", "module_ready name=rtc");
    }
#endif
```

Modify `Esp32Base::handle()` after core runtime maintenance and before deferred network starts:

```cpp
#if ESP32BASE_ENABLE_RTC
    Esp32BaseRtc::handle();
#endif
```

Add bottom include:

```cpp
#if ESP32BASE_ENABLE_RTC
#include "runtime/Esp32BaseRtc.inc"
#endif
```

- [ ] **Step 8: Run tests**

Run:

```bash
pio test -e native_time_harness
```

Expected: all native time harness tests pass; RTC status is `missing` because the temporary binding has no chip driver.

- [ ] **Step 9: Commit common RTC shell**

```bash
git add src/runtime/Esp32BaseRtc.h src/runtime/Esp32BaseRtc.inc src/runtime/internal/Esp32BaseRtcDrivers.h src/Esp32BaseProfile.h src/Esp32Base.h src/Esp32Base.cpp test/test_native_time_harness/test_main.cpp
git commit -m "feat: add rtc time source facade"
```

---

### Task 5: DS3231 Driver

**Files:**
- Create: `src/runtime/internal/Esp32BaseRtcCalendar.h`
- Create: `src/runtime/internal/Esp32BaseRtcBus.h`
- Create: `src/runtime/internal/Esp32BaseRtcDs3231.inc`
- Modify: `src/runtime/Esp32BaseRtc.inc`
- Modify: `test/test_native_time_harness/test_main.cpp`

- [ ] **Step 1: Add failing DS3231 tests**

Append DS3231 register helpers to the test file:

```cpp
static uint8_t bcd(uint8_t value) {
    return static_cast<uint8_t>(((value / 10) << 4) | (value % 10));
}

static void loadDs3231Time() {
    Wire.devices[0x68] = std::vector<uint8_t>(32, 0);
    Wire.devices[0x68][0x00] = bcd(5);
    Wire.devices[0x68][0x01] = bcd(4);
    Wire.devices[0x68][0x02] = bcd(3);
    Wire.devices[0x68][0x03] = bcd(2);
    Wire.devices[0x68][0x04] = bcd(2);
    Wire.devices[0x68][0x05] = bcd(1);
    Wire.devices[0x68][0x06] = bcd(24);
    Wire.devices[0x68][0x0F] = 0x00;
}

void test_ds3231_reads_valid_epoch() {
#if ESP32BASE_RTC_DRIVER == ESP32BASE_RTC_DRIVER_DS3231
    resetTimeHarness();
    Wire.devices.clear();
    loadDs3231Time();

    TEST_ASSERT_TRUE(Esp32BaseRtc::configure(Wire, 0x68));
    uint32_t epoch = 0;
    TEST_ASSERT_TRUE(Esp32BaseRtc::readEpoch(&epoch));
    TEST_ASSERT_EQUAL(Esp32BaseRtc::STATUS_OK, Esp32BaseRtc::status());
    TEST_ASSERT_EQUAL_UINT32(1704164645UL, epoch);
#endif
}

void test_ds3231_osf_marks_clock_stopped() {
#if ESP32BASE_RTC_DRIVER == ESP32BASE_RTC_DRIVER_DS3231
    resetTimeHarness();
    Wire.devices.clear();
    loadDs3231Time();
    Wire.devices[0x68][0x0F] = 0x80;

    TEST_ASSERT_TRUE(Esp32BaseRtc::configure(Wire, 0x68));
    uint32_t epoch = 0;
    TEST_ASSERT_FALSE(Esp32BaseRtc::readEpoch(&epoch));
    TEST_ASSERT_EQUAL(Esp32BaseRtc::STATUS_CLOCK_STOPPED, Esp32BaseRtc::status());
#endif
}
```

Add to `main()`:

```cpp
RUN_TEST(test_ds3231_reads_valid_epoch);
RUN_TEST(test_ds3231_osf_marks_clock_stopped);
```

- [ ] **Step 2: Run tests and verify failure**

Run:

```bash
pio test -e native_time_harness
```

Expected: DS3231 tests fail because the temporary driver binding returns `STATUS_MISSING`.

- [ ] **Step 3: Add shared RTC calendar and I2C helpers**

Create `src/runtime/internal/Esp32BaseRtcCalendar.h`:

```cpp
#pragma once

#include <stdint.h>

namespace esp32base_internal {

inline uint8_t rtcToBcd(uint8_t value) {
    return static_cast<uint8_t>(((value / 10U) << 4U) | (value % 10U));
}

inline bool rtcFromBcd(uint8_t bcdValue, uint8_t maxValue, uint8_t* out) {
    const uint8_t hi = (bcdValue >> 4U) & 0x0FU;
    const uint8_t lo = bcdValue & 0x0FU;
    if (hi > 9 || lo > 9) {
        return false;
    }
    const uint8_t value = static_cast<uint8_t>(hi * 10U + lo);
    if (value > maxValue) {
        return false;
    }
    *out = value;
    return true;
}

inline bool rtcLeapYear(uint16_t year) {
    return (year % 4U == 0U && year % 100U != 0U) || (year % 400U == 0U);
}

inline uint8_t rtcDaysInMonth(uint16_t year, uint8_t month) {
    static const uint8_t days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (month < 1 || month > 12) {
        return 0;
    }
    if (month == 2 && rtcLeapYear(year)) {
        return 29;
    }
    return days[month - 1];
}

inline bool epochFromUtcFields(uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute, uint8_t second, uint32_t* epoch) {
    if (!epoch || year < 2000 || year > 2099 || hour > 23 || minute > 59 || second > 59) {
        return false;
    }
    const uint8_t maxDay = rtcDaysInMonth(year, month);
    if (day < 1 || day > maxDay) {
        return false;
    }
    uint32_t days = 0;
    for (uint16_t y = 1970; y < year; ++y) {
        days += rtcLeapYear(y) ? 366UL : 365UL;
    }
    for (uint8_t m = 1; m < month; ++m) {
        days += rtcDaysInMonth(year, m);
    }
    days += static_cast<uint32_t>(day - 1);
    *epoch = days * 86400UL + static_cast<uint32_t>(hour) * 3600UL + static_cast<uint32_t>(minute) * 60UL + second;
    return true;
}

inline bool utcFieldsFromEpoch(uint32_t epoch, uint16_t* year, uint8_t* month, uint8_t* day, uint8_t* hour, uint8_t* minute, uint8_t* second) {
    if (!year || !month || !day || !hour || !minute || !second) {
        return false;
    }
    uint32_t days = epoch / 86400UL;
    uint32_t rem = epoch % 86400UL;
    *hour = static_cast<uint8_t>(rem / 3600UL);
    rem %= 3600UL;
    *minute = static_cast<uint8_t>(rem / 60UL);
    *second = static_cast<uint8_t>(rem % 60UL);

    uint16_t y = 1970;
    while (true) {
        const uint16_t yd = rtcLeapYear(y) ? 366U : 365U;
        if (days < yd) {
            break;
        }
        days -= yd;
        ++y;
    }
    uint8_t m = 1;
    while (true) {
        const uint8_t md = rtcDaysInMonth(y, m);
        if (days < md) {
            break;
        }
        days -= md;
        ++m;
    }
    *year = y;
    *month = m;
    *day = static_cast<uint8_t>(days + 1U);
    return y >= 2000 && y <= 2099;
}

}
```

Create `src/runtime/internal/Esp32BaseRtcBus.h`:

```cpp
#pragma once

#include <stdint.h>
#include <Wire.h>

namespace esp32base_internal {

inline bool rtcReadRegs(TwoWire& wire, uint8_t address, uint8_t reg, uint8_t* data, uint8_t len) {
    wire.beginTransmission(address);
    wire.write(reg);
    if (wire.endTransmission() != 0) {
        return false;
    }
    if (wire.requestFrom(address, len) != len) {
        return false;
    }
    for (uint8_t i = 0; i < len; ++i) {
        const int v = wire.read();
        if (v < 0) {
            return false;
        }
        data[i] = static_cast<uint8_t>(v);
    }
    return true;
}

inline bool rtcWriteRegs(TwoWire& wire, uint8_t address, uint8_t reg, const uint8_t* data, uint8_t len) {
    wire.beginTransmission(address);
    wire.write(reg);
    for (uint8_t i = 0; i < len; ++i) {
        wire.write(data[i]);
    }
    return wire.endTransmission() == 0;
}

}
```

- [ ] **Step 4: Implement DS3231 driver**

Create `src/runtime/internal/Esp32BaseRtcDs3231.inc`:

```cpp
#include "Esp32BaseRtcBus.h"
#include "Esp32BaseRtcCalendar.h"

namespace {
using esp32base_internal::epochFromUtcFields;
using esp32base_internal::rtcFromBcd;
using esp32base_internal::rtcReadRegs;
using esp32base_internal::rtcToBcd;
using esp32base_internal::rtcWriteRegs;
using esp32base_internal::utcFieldsFromEpoch;

bool ds3231Probe(TwoWire& wire, uint8_t address) {
    uint8_t status = 0;
    return rtcReadRegs(wire, address, 0x0F, &status, 1);
}

bool ds3231ReadEpoch(TwoWire& wire, uint8_t address, uint32_t* epoch, Esp32BaseRtc::Status* status) {
    uint8_t regs[7];
    uint8_t stat = 0;
    if (!rtcReadRegs(wire, address, 0x00, regs, sizeof(regs))) {
        *status = Esp32BaseRtc::STATUS_MISSING;
        return false;
    }
    if (!rtcReadRegs(wire, address, 0x0F, &stat, 1)) {
        *status = Esp32BaseRtc::STATUS_I2C_ERROR;
        return false;
    }
    if (stat & 0x80U) {
        *status = Esp32BaseRtc::STATUS_CLOCK_STOPPED;
        return false;
    }
    uint8_t second, minute, hour, day, month, year2;
    if (!rtcFromBcd(regs[0] & 0x7F, 59, &second) ||
        !rtcFromBcd(regs[1] & 0x7F, 59, &minute) ||
        !rtcFromBcd(regs[2] & 0x3F, 23, &hour) ||
        !rtcFromBcd(regs[4] & 0x3F, 31, &day) ||
        !rtcFromBcd(regs[5] & 0x1F, 12, &month) ||
        !rtcFromBcd(regs[6], 99, &year2)) {
        *status = Esp32BaseRtc::STATUS_TIME_INVALID;
        return false;
    }
    const uint16_t year = static_cast<uint16_t>(2000U + year2);
    if (!epochFromUtcFields(year, month, day, hour, minute, second, epoch)) {
        *status = Esp32BaseRtc::STATUS_TIME_INVALID;
        return false;
    }
    *status = Esp32BaseRtc::STATUS_OK;
    return true;
}

bool ds3231WriteEpoch(TwoWire& wire, uint8_t address, uint32_t epoch, Esp32BaseRtc::Status* status) {
    uint16_t year;
    uint8_t month, day, hour, minute, second;
    if (!utcFieldsFromEpoch(epoch, &year, &month, &day, &hour, &minute, &second) || year < 2000 || year > 2099) {
        *status = Esp32BaseRtc::STATUS_TIME_INVALID;
        return false;
    }
    const uint8_t regs[7] = {
        rtcToBcd(second), rtcToBcd(minute), rtcToBcd(hour),
        1, rtcToBcd(day), rtcToBcd(month), rtcToBcd(static_cast<uint8_t>(year - 2000))
    };
    if (!rtcWriteRegs(wire, address, 0x00, regs, sizeof(regs))) {
        *status = Esp32BaseRtc::STATUS_I2C_ERROR;
        return false;
    }
    uint8_t stat = 0;
    if (rtcReadRegs(wire, address, 0x0F, &stat, 1)) {
        stat &= static_cast<uint8_t>(~0x80U);
        rtcWriteRegs(wire, address, 0x0F, &stat, 1);
    }
    *status = Esp32BaseRtc::STATUS_OK;
    return true;
}

const Esp32BaseRtcDriverOps kDs3231Ops = {
    "ds3231",
    0x68,
    ds3231Probe,
    ds3231ReadEpoch,
    ds3231WriteEpoch
};
}
```

- [ ] **Step 5: Replace temporary binding with DS3231 driver binding**

Modify `src/runtime/Esp32BaseRtc.inc` by removing the temporary `kRtcMissingDs3231Ops` / `kRtcMissingPcf8563Ops` binding and adding:

```cpp
#if ESP32BASE_RTC_DRIVER == ESP32BASE_RTC_DRIVER_DS3231
#include "internal/Esp32BaseRtcDs3231.inc"
#endif

namespace {
const Esp32BaseRtcDriverOps* rtcDriverOps() {
#if ESP32BASE_RTC_DRIVER == ESP32BASE_RTC_DRIVER_DS3231
    return &kDs3231Ops;
#else
    return nullptr;
#endif
}
}
```

- [ ] **Step 6: Run DS3231 tests**

Run:

```bash
pio test -e native_time_harness
```

Expected: DS3231 read and OSF tests pass.

- [ ] **Step 7: Commit DS3231 support**

```bash
git add src/runtime/internal/Esp32BaseRtcCalendar.h src/runtime/internal/Esp32BaseRtcBus.h src/runtime/internal/Esp32BaseRtcDs3231.inc src/runtime/Esp32BaseRtc.inc test/test_native_time_harness/test_main.cpp
git commit -m "feat: add ds3231 rtc driver"
```

---

### Task 6: PCF8563 Driver

**Files:**
- Create: `src/runtime/internal/Esp32BaseRtcPcf8563.inc`
- Modify: `src/runtime/Esp32BaseRtc.inc`
- Modify: `test/test_native_time_harness/test_main.cpp`
- Modify: `platformio.ini`

- [ ] **Step 1: Add PCF8563 native env**

Add to `platformio.ini`:

```ini
[env:native_time_pcf8563_harness]
extends = env:native_time_harness
build_flags =
  ${env:native_time_harness.build_flags}
  -D ESP32BASE_RTC_DRIVER=ESP32BASE_RTC_DRIVER_PCF8563
test_filter = test_native_time_harness
```

- [ ] **Step 2: Add failing PCF8563 tests**

Append to `test/test_native_time_harness/test_main.cpp`:

```cpp
static void loadPcf8563Time() {
    Wire.devices[0x51] = std::vector<uint8_t>(32, 0);
    Wire.devices[0x51][0x02] = bcd(5);
    Wire.devices[0x51][0x03] = bcd(4);
    Wire.devices[0x51][0x04] = bcd(3);
    Wire.devices[0x51][0x05] = bcd(2);
    Wire.devices[0x51][0x06] = bcd(2);
    Wire.devices[0x51][0x07] = bcd(1);
    Wire.devices[0x51][0x08] = bcd(24);
}

void test_pcf8563_reads_valid_epoch_when_selected() {
#if ESP32BASE_RTC_DRIVER == ESP32BASE_RTC_DRIVER_PCF8563
    resetTimeHarness();
    Wire.devices.clear();
    loadPcf8563Time();

    TEST_ASSERT_TRUE(Esp32BaseRtc::configure(Wire, 0x51));
    uint32_t epoch = 0;
    TEST_ASSERT_TRUE(Esp32BaseRtc::readEpoch(&epoch));
    TEST_ASSERT_EQUAL(Esp32BaseRtc::STATUS_OK, Esp32BaseRtc::status());
    TEST_ASSERT_EQUAL_UINT32(1704164645UL, epoch);
#endif
}

void test_pcf8563_vl_marks_clock_stopped_when_selected() {
#if ESP32BASE_RTC_DRIVER == ESP32BASE_RTC_DRIVER_PCF8563
    resetTimeHarness();
    Wire.devices.clear();
    loadPcf8563Time();
    Wire.devices[0x51][0x02] |= 0x80;

    TEST_ASSERT_TRUE(Esp32BaseRtc::configure(Wire, 0x51));
    uint32_t epoch = 0;
    TEST_ASSERT_FALSE(Esp32BaseRtc::readEpoch(&epoch));
    TEST_ASSERT_EQUAL(Esp32BaseRtc::STATUS_CLOCK_STOPPED, Esp32BaseRtc::status());
#endif
}
```

Add to `main()`:

```cpp
RUN_TEST(test_pcf8563_reads_valid_epoch_when_selected);
RUN_TEST(test_pcf8563_vl_marks_clock_stopped_when_selected);
```

- [ ] **Step 3: Run PCF8563 tests and verify failure**

Run:

```bash
pio test -e native_time_pcf8563_harness
```

Expected: PCF8563 tests fail because the driver is not implemented or not bound.

- [ ] **Step 4: Implement PCF8563 driver**

Create `src/runtime/internal/Esp32BaseRtcPcf8563.inc`:

```cpp
#include "Esp32BaseRtcBus.h"
#include "Esp32BaseRtcCalendar.h"

namespace {
using esp32base_internal::epochFromUtcFields;
using esp32base_internal::rtcFromBcd;
using esp32base_internal::rtcReadRegs;
using esp32base_internal::rtcToBcd;
using esp32base_internal::rtcWriteRegs;
using esp32base_internal::utcFieldsFromEpoch;

bool pcf8563Probe(TwoWire& wire, uint8_t address) {
    uint8_t control = 0;
    return rtcReadRegs(wire, address, 0x00, &control, 1);
}

bool pcf8563ReadEpoch(TwoWire& wire, uint8_t address, uint32_t* epoch, Esp32BaseRtc::Status* status) {
    uint8_t regs[7];
    if (!rtcReadRegs(wire, address, 0x02, regs, sizeof(regs))) {
        *status = Esp32BaseRtc::STATUS_MISSING;
        return false;
    }
    if (regs[0] & 0x80U) {
        *status = Esp32BaseRtc::STATUS_CLOCK_STOPPED;
        return false;
    }
    uint8_t second, minute, hour, day, month, year2;
    if (!rtcFromBcd(regs[0] & 0x7F, 59, &second) ||
        !rtcFromBcd(regs[1] & 0x7F, 59, &minute) ||
        !rtcFromBcd(regs[2] & 0x3F, 23, &hour) ||
        !rtcFromBcd(regs[3] & 0x3F, 31, &day) ||
        !rtcFromBcd(regs[5] & 0x1F, 12, &month) ||
        !rtcFromBcd(regs[6], 99, &year2)) {
        *status = Esp32BaseRtc::STATUS_TIME_INVALID;
        return false;
    }
    const uint16_t year = static_cast<uint16_t>(2000U + year2);
    if (!epochFromUtcFields(year, month, day, hour, minute, second, epoch)) {
        *status = Esp32BaseRtc::STATUS_TIME_INVALID;
        return false;
    }
    *status = Esp32BaseRtc::STATUS_OK;
    return true;
}

bool pcf8563WriteEpoch(TwoWire& wire, uint8_t address, uint32_t epoch, Esp32BaseRtc::Status* status) {
    uint16_t year;
    uint8_t month, day, hour, minute, second;
    if (!utcFieldsFromEpoch(epoch, &year, &month, &day, &hour, &minute, &second) || year < 2000 || year > 2099) {
        *status = Esp32BaseRtc::STATUS_TIME_INVALID;
        return false;
    }
    const uint8_t regs[7] = {
        rtcToBcd(second), rtcToBcd(minute), rtcToBcd(hour),
        rtcToBcd(day), 1, rtcToBcd(month), rtcToBcd(static_cast<uint8_t>(year - 2000))
    };
    if (!rtcWriteRegs(wire, address, 0x02, regs, sizeof(regs))) {
        *status = Esp32BaseRtc::STATUS_I2C_ERROR;
        return false;
    }
    *status = Esp32BaseRtc::STATUS_OK;
    return true;
}

const Esp32BaseRtcDriverOps kPcf8563Ops = {
    "pcf8563",
    0x51,
    pcf8563Probe,
    pcf8563ReadEpoch,
    pcf8563WriteEpoch
};
}
```

- [ ] **Step 5: Bind PCF8563 driver**

Modify `src/runtime/Esp32BaseRtc.inc`:

```cpp
#if ESP32BASE_RTC_DRIVER == ESP32BASE_RTC_DRIVER_PCF8563
#include "internal/Esp32BaseRtcPcf8563.inc"
#endif
```

Update `rtcDriverOps()`:

```cpp
const Esp32BaseRtcDriverOps* rtcDriverOps() {
#if ESP32BASE_RTC_DRIVER == ESP32BASE_RTC_DRIVER_DS3231
    return &kDs3231Ops;
#elif ESP32BASE_RTC_DRIVER == ESP32BASE_RTC_DRIVER_PCF8563
    return &kPcf8563Ops;
#else
    return nullptr;
#endif
}
```

- [ ] **Step 6: Run both RTC harnesses**

Run:

```bash
pio test -e native_time_harness
pio test -e native_time_pcf8563_harness
```

Expected: both pass.

- [ ] **Step 7: Commit PCF8563 support**

```bash
git add platformio.ini src/runtime/internal/Esp32BaseRtcPcf8563.inc src/runtime/Esp32BaseRtc.inc test/test_native_time_harness/test_main.cpp
git commit -m "feat: add pcf8563 rtc driver"
```

---

### Task 7: RTC Calendar Edge-Case Regression Tests

**Files:**
- Modify: `test/test_native_time_harness/test_main.cpp`

- [ ] **Step 1: Add conversion regression tests**

Append:

```cpp
void test_rtc_rejects_impossible_calendar_date() {
    resetTimeHarness();
    Wire.devices.clear();
#if ESP32BASE_RTC_DRIVER == ESP32BASE_RTC_DRIVER_DS3231
    loadDs3231Time();
    Wire.devices[0x68][0x04] = bcd(31);
    Wire.devices[0x68][0x05] = bcd(2);
    TEST_ASSERT_TRUE(Esp32BaseRtc::configure(Wire, 0x68));
#else
    loadPcf8563Time();
    Wire.devices[0x51][0x05] = bcd(31);
    Wire.devices[0x51][0x07] = bcd(2);
    TEST_ASSERT_TRUE(Esp32BaseRtc::configure(Wire, 0x51));
#endif
    uint32_t epoch = 0;
    TEST_ASSERT_FALSE(Esp32BaseRtc::readEpoch(&epoch));
    TEST_ASSERT_EQUAL(Esp32BaseRtc::STATUS_TIME_INVALID, Esp32BaseRtc::status());
}

void test_rtc_set_epoch_round_trips_selected_driver() {
    resetTimeHarness();
    Wire.devices.clear();
#if ESP32BASE_RTC_DRIVER == ESP32BASE_RTC_DRIVER_DS3231
    loadDs3231Time();
    TEST_ASSERT_TRUE(Esp32BaseRtc::configure(Wire, 0x68));
#else
    loadPcf8563Time();
    TEST_ASSERT_TRUE(Esp32BaseRtc::configure(Wire, 0x51));
#endif
    TEST_ASSERT_TRUE(Esp32BaseRtc::setEpoch(1709251199UL));

    uint32_t epoch = 0;
    TEST_ASSERT_TRUE(Esp32BaseRtc::readEpoch(&epoch));
    TEST_ASSERT_EQUAL_UINT32(1709251199UL, epoch);
}
```

Add to `main()`:

```cpp
RUN_TEST(test_rtc_rejects_impossible_calendar_date);
RUN_TEST(test_rtc_set_epoch_round_trips_selected_driver);
```

- [ ] **Step 2: Run both RTC harnesses**

Run:

```bash
pio test -e native_time_harness
pio test -e native_time_pcf8563_harness
```

Expected: both pass; impossible dates are rejected and `setEpoch()` round-trips through the selected chip driver.

- [ ] **Step 3: Commit conversion regression tests**

```bash
git add test/test_native_time_harness/test_main.cpp
git commit -m "test: cover rtc calendar edge cases"
```

---

### Task 8: NTP Integration and RTC Write-Back

**Files:**
- Modify: `src/network/Esp32BaseNtp.h`
- Modify: `src/network/Esp32BaseNtp.inc`
- Modify: `src/runtime/Esp32BaseRtc.inc`
- Create: `src/runtime/internal/Esp32BaseRtcInternal.h`
- Modify: `test/test_native_time_harness/test_main.cpp`

- [ ] **Step 1: Add failing write-back decision test**

Append:

```cpp
void test_ntp_writeback_updates_rtc_when_drift_exceeds_threshold() {
    resetTimeHarness();
    Wire.devices.clear();
#if ESP32BASE_RTC_DRIVER == ESP32BASE_RTC_DRIVER_DS3231
    loadDs3231Time();
    TEST_ASSERT_TRUE(Esp32BaseRtc::configure(Wire, 0x68));
#else
    loadPcf8563Time();
    TEST_ASSERT_TRUE(Esp32BaseRtc::configure(Wire, 0x51));
#endif
    TEST_ASSERT_TRUE(Esp32BaseRtc::begin());

    TEST_ASSERT_TRUE(esp32base_internal::timeAcceptNtpEpoch(1704164655UL));
    TEST_ASSERT_TRUE(esp32base_internal::rtcWriteBackFromNtp(1704164655UL));

    uint32_t epoch = 0;
    TEST_ASSERT_TRUE(Esp32BaseRtc::readEpoch(&epoch));
    TEST_ASSERT_EQUAL_UINT32(1704164655UL, epoch);
}
```

Add to `main()`:

```cpp
RUN_TEST(test_ntp_writeback_updates_rtc_when_drift_exceeds_threshold);
```

- [ ] **Step 2: Run failing tests**

Run:

```bash
pio test -e native_time_harness
pio test -e native_time_pcf8563_harness
```

Expected: compile fails because `rtcWriteBackFromNtp` does not exist.

- [ ] **Step 3: Add internal RTC write-back hook**

Create `src/runtime/internal/Esp32BaseRtcInternal.h`:

```cpp
#pragma once

#include <stdint.h>

namespace esp32base_internal {
bool rtcWriteBackFromNtp(uint32_t ntpEpochSec);
}
```

Implement in `src/runtime/Esp32BaseRtc.inc`:

```cpp
namespace esp32base_internal {
bool rtcWriteBackFromNtp(uint32_t ntpEpochSec) {
#if ESP32BASE_RTC_NTP_WRITEBACK
    uint32_t rtcEpoch = 0;
    if (!Esp32BaseRtc::readEpoch(&rtcEpoch)) {
        return false;
    }
    const uint32_t diff = ntpEpochSec > rtcEpoch ? ntpEpochSec - rtcEpoch : rtcEpoch - ntpEpochSec;
    if (diff <= ESP32BASE_RTC_WRITEBACK_THRESHOLD_SEC) {
        return true;
    }
    return Esp32BaseRtc::setEpoch(ntpEpochSec);
#else
    (void)ntpEpochSec;
    return true;
#endif
}
}
```

- [ ] **Step 4: Route NTP trusted sync into Time**

In `src/network/Esp32BaseNtp.inc`, include the internal hooks:

```cpp
#include "../runtime/Esp32BaseTime.h"
#include "../runtime/internal/Esp32BaseTimeInternal.h"
#if ESP32BASE_ENABLE_RTC
#include "../runtime/internal/Esp32BaseRtcInternal.h"
#endif
```

In `src/network/Esp32BaseNtp.inc`, when SNTP completes and `now` is trusted, replace direct boot mapping state update with:

```cpp
Esp32BaseTime::initBootSession();
esp32base_internal::timeAcceptNtpEpoch(now);
#if ESP32BASE_ENABLE_RTC
esp32base_internal::rtcWriteBackFromNtp(now);
#endif
```

Keep existing NTP logging and `g_timeSyncCallback`, but derive boot wall time from `Esp32BaseTime::snapshot()` instead of NTP-private `g_bootStartEpochSec`. After accepting NTP time, set `g_loggedSync = true` and call `g_timeSyncCallback(snapshot())` as before so existing business callbacks still run. Remove NTP-private boot mapping state (`g_bootSessionReady`, `g_bootId`, `g_bootStartEpochSec`) after the compatibility methods below delegate to `Esp32BaseTime`.

- [ ] **Step 5: Keep compatibility APIs**

Update `Esp32BaseNtp::initBootSession()`:

```cpp
bool Esp32BaseNtp::initBootSession() {
    return Esp32BaseTime::initBootSession();
}
```

Update `Esp32BaseNtp::snapshot()`:

```cpp
Esp32BaseNtp::TimeSnapshot Esp32BaseNtp::snapshot() {
    const Esp32BaseTime::Snapshot time = Esp32BaseTime::snapshot();
    TimeSnapshot value = {};
    value.synced = time.synced;
    value.epochSec = time.epochSec;
    value.uptimeSec = time.uptimeSec;
    value.bootId = time.bootId;
    value.bootStartEpochSec = time.bootStartEpochSec;
    return value;
}
```

Update `Esp32BaseNtp::resolveCurrentBootEvent(...)` to call `Esp32BaseTime::resolveCurrentBootEvent(...)`.

Update `Esp32BaseNtp::isCurrentBootEvent(...)` and `Esp32BaseNtp::canResolveCurrentBootEvent(...)`:

```cpp
bool Esp32BaseNtp::isCurrentBootEvent(uint32_t bootId) {
    const Esp32BaseTime::Snapshot time = Esp32BaseTime::snapshot();
    return bootId != 0 && bootId == time.bootId;
}

bool Esp32BaseNtp::canResolveCurrentBootEvent(uint32_t bootId) {
    const Esp32BaseTime::Snapshot time = Esp32BaseTime::snapshot();
    return bootId != 0 && bootId == time.bootId && time.synced && time.bootStartEpochSec != 0;
}
```

- [ ] **Step 6: Run native time tests**

Run:

```bash
pio test -e native_time_harness
pio test -e native_time_pcf8563_harness
```

Expected: both pass.

- [ ] **Step 7: Commit NTP integration**

```bash
git add src/network/Esp32BaseNtp.h src/network/Esp32BaseNtp.inc src/runtime/Esp32BaseRtc.inc src/runtime/internal/Esp32BaseRtcInternal.h test/test_native_time_harness/test_main.cpp
git commit -m "feat: route ntp through trusted time"
```

---

### Task 9: App Events, Logs, and Web Time Consumers

**Files:**
- Modify: `src/Esp32Base.cpp`
- Modify: `src/runtime/Esp32BaseAppEventLog.h`
- Modify: `src/runtime/Esp32BaseAppEventLog.inc`
- Modify: `src/web/internal/WebContext.h`
- Modify: `src/web/internal/WebStatus.cpp`
- Modify: `src/web/internal/WebAppEvents.cpp`
- Modify: `src/web/internal/WebFs.cpp`
- Modify: `src/web/internal/WebInternal.h`

- [ ] **Step 1: Replace App Events NTP provider**

In `src/Esp32Base.cpp`, replace `appEventLogTimeFromNtp()` with:

```cpp
#if ESP32BASE_ENABLE_APP_EVENTS && ESP32BASE_ENABLE_TIME
Esp32BaseAppEventLog::TimeSnapshot appEventLogTimeFromTrustedTime() {
    const Esp32BaseTime::Snapshot time = Esp32BaseTime::snapshot();
    Esp32BaseAppEventLog::TimeSnapshot value = {};
    value.synced = time.synced;
    value.epochSec = time.epochSec;
    value.bootId = time.bootId;
    value.uptimeSec = time.uptimeSec;
    return value;
}
#endif
```

Change provider setup:

```cpp
#if ESP32BASE_ENABLE_APP_EVENTS && ESP32BASE_ENABLE_TIME
    Esp32BaseAppEventLog::setTimeProvider(appEventLogTimeFromTrustedTime);
#endif
```

- [ ] **Step 2: Update Web includes**

In `src/web/internal/WebContext.h`, replace NTP-only include with Time and conditional RTC include:

```cpp
#if ESP32BASE_ENABLE_TIME
#include "../../runtime/Esp32BaseTime.h"
#endif
#if ESP32BASE_ENABLE_RTC
#include "../../runtime/Esp32BaseRtc.h"
#endif
#if ESP32BASE_ENABLE_NTP
#include "../../network/Esp32BaseNtp.h"
#endif
```

- [ ] **Step 3: Update Watchdog trip reset time**

In `src/web/internal/WebStatus.cpp`, replace:

```cpp
const Esp32BaseNtp::TimeSnapshot time = Esp32BaseNtp::snapshot();
```

with:

```cpp
const Esp32BaseTime::Snapshot time = Esp32BaseTime::snapshot();
```

Return `time.epochSec` only when `time.synced`.

- [ ] **Step 4: Update Status time row**

Replace the "NTP time" row with "Time":

```cpp
sendInfoRowStart("Time");
if (time.synced && Esp32BaseTime::formatTime(value, sizeof(value), "%Y-%m-%d %H:%M:%S")) {
    sendEscapedHtmlChunk(value);
    sendChunk("<span class='sub'>source ");
    sendEscapedHtmlChunk(Esp32BaseTime::sourceName(time.source));
    sendChunk("</span>");
} else {
    sendEscapedHtmlChunk("not synced");
}
sendInfoRowEnd();
```

When RTC is enabled, add submetrics:

```cpp
#if ESP32BASE_ENABLE_RTC
sendInfoRowStart("RTC");
sendEscapedHtmlChunk(Esp32BaseRtc::driverName());
sendChunk("<span class='sub'>");
sendEscapedHtmlChunk(Esp32BaseRtc::statusText());
sendChunk("</span>");
sendInfoRowEnd();
#endif
```

- [ ] **Step 5: Update Web App Events resolution**

In `src/web/internal/WebAppEvents.cpp`, replace:

```cpp
Esp32BaseNtp::resolveCurrentBootEvent(event.bootId, event.uptimeSec, &epoch)
```

with:

```cpp
Esp32BaseTime::resolveCurrentBootEvent(event.bootId, event.uptimeSec, &epoch)
```

Update UI helper text from "NTP" to "trusted real time".

- [ ] **Step 6: Run native harnesses**

Run:

```bash
pio test -e native_time_harness
pio test -e native_time_pcf8563_harness
pio test -e native_web_harness
pio test -e native_config_harness
```

Expected: all pass.

- [ ] **Step 7: Commit internal time consumer migration**

```bash
git add src/Esp32Base.cpp src/runtime/Esp32BaseAppEventLog.h src/runtime/Esp32BaseAppEventLog.inc src/web/internal
git commit -m "feat: use trusted time across diagnostics"
```

---

### Task 10: Examples and Documentation

**Files:**
- Create: `examples/rtc_time_source/src/main.cpp`
- Create: `examples/rtc_time_source/platformio.ini`
- Modify: `README.md`
- Modify: `docs/01_architecture.md`
- Modify: `docs/02_profiles.md`
- Modify: `docs/03_api.md`
- Modify: `docs/04_web.md`
- Modify: `docs/07_diagnostics.md`
- Modify: `docs/08_arduino_core_compat.md`
- Modify: `docs/10_known_limitations.md`

- [ ] **Step 1: Add RTC example**

Create `examples/rtc_time_source/src/main.cpp`:

```cpp
#include <Arduino.h>
#include <Wire.h>
#include <Esp32Base.h>

void setup() {
    Serial.begin(115200);
    Wire.begin(21, 22);
    Esp32Base::setFirmwareInfo("rtc-time-source", "1.0.0");
    Esp32Base::begin();
}

void loop() {
    Esp32Base::handle();

    static uint32_t last = 0;
    if (millis() - last >= 5000) {
        last = millis();
        const Esp32BaseTime::Snapshot time = Esp32BaseTime::snapshot();
        char text[32];
        if (Esp32BaseTime::formatTime(text, sizeof(text), "%Y-%m-%d %H:%M:%S")) {
            Serial.printf("time=%s source=%s rtc=%s\n",
                          text,
                          Esp32BaseTime::sourceName(time.source),
                          Esp32BaseRtc::statusText());
        } else {
            Serial.printf("uptime=%lu rtc=%s\n",
                          static_cast<unsigned long>(time.uptimeSec),
                          Esp32BaseRtc::statusText());
        }
    }
}
```

Create `examples/rtc_time_source/platformio.ini` with DS3231 and PCF8563 envs:

```ini
[platformio]
default_envs = esp32_ds3231

[env]
platform = platformio/espressif32@6.7.0
board = esp32dev
framework = arduino
monitor_speed = 115200
lib_extra_dirs = ../..
lib_deps =
  WiFi
  DNSServer
  ESPmDNS
build_flags =
  -D ESP32BASE_PROFILE=ESP32BASE_PROFILE_NET
  -D ESP32BASE_ENABLE_RTC=1
  -D ESP32BASE_RTC_AUTO_WIRE_BEGIN=0
  -I ${platformio.packages_dir}/framework-arduinoespressif32/libraries/Wire/src
  -I ${platformio.packages_dir}/framework-arduinoespressif32/libraries/WiFi/src
  -I ${platformio.packages_dir}/framework-arduinoespressif32/libraries/DNSServer/src
  -I ${platformio.packages_dir}/framework-arduinoespressif32/libraries/ESPmDNS/src

[env:esp32_ds3231]
build_flags =
  ${env.build_flags}
  -D ESP32BASE_RTC_DRIVER=ESP32BASE_RTC_DRIVER_DS3231

[env:esp32_pcf8563]
build_flags =
  ${env.build_flags}
  -D ESP32BASE_RTC_DRIVER=ESP32BASE_RTC_DRIVER_PCF8563
```

- [ ] **Step 2: Update README and architecture docs**

Add a short README section covering:

```markdown
### RTC time source

RTC support is optional. Enable it only for boards with a battery-backed DS3231 or PCF8563:

    #define ESP32BASE_ENABLE_RTC 1
    #define ESP32BASE_RTC_DRIVER ESP32BASE_RTC_DRIVER_DS3231

Time authority is `NTP > RTC > uptime`. Esp32Base uses RTC only for trusted wall-clock time and NTP write-back; applications may still use I2C directly for alarms and INT/SQW.
```

- [ ] **Step 3: Update API docs**

In `docs/03_api.md`, add sections for `Esp32BaseTime` and `Esp32BaseRtc` using the public class declarations from Tasks 2 and 4. Replace NTP-only wording for App Events and Status with "trusted real time".

- [ ] **Step 4: Update known limitations**

In `docs/10_known_limitations.md`, add:

```markdown
- RTC support does not auto-detect chips. Applications must configure DS3231 or PCF8563 at build time.
- RTC support does not own alarm, INT/SQW, timer, temperature, or calibration features.
- RTC status is cached by default; periodic polling is opt-in with `ESP32BASE_RTC_STATUS_REFRESH_MS`.
```

- [ ] **Step 5: Build example and run doc-sensitive tests**

Run:

```bash
pio run -d examples/rtc_time_source -e esp32_ds3231
pio run -d examples/rtc_time_source -e esp32_pcf8563
pio test -e native_time_harness
pio test -e native_time_pcf8563_harness
```

Expected: all commands pass.

- [ ] **Step 6: Commit docs and example**

```bash
git add README.md docs examples/rtc_time_source
git commit -m "docs: add rtc time source usage"
```

---

### Task 11: Trim, Builds, and Regression Verification

**Files:**
- Modify: `scripts/check_trim_symbols.py` only if RTC symbol checks need explicit support.
- Modify: `docs/09_release_checklist.md` if release checklist needs RTC validation notes.

- [ ] **Step 1: Run native test suite**

Run:

```bash
pio test -e native_time_harness
pio test -e native_time_pcf8563_harness
pio test -e native_web_harness
pio test -e native_config_harness
```

Expected: all tests pass.

- [ ] **Step 2: Build trim-sensitive examples**

Run:

```bash
pio run -d examples/basic -e esp32_core
pio run -d examples/basic -e esp32_net
pio run -d examples/basic -e esp32_full
pio run -d examples/rtc_time_source -e esp32_ds3231
pio run -d examples/rtc_time_source -e esp32_pcf8563
```

Expected: all builds pass.

- [ ] **Step 3: Check RTC symbols are trimmed when disabled**

Run:

```bash
python scripts/check_trim_symbols.py examples/basic/.pio/build/esp32_core/firmware.elf --forbid Esp32BaseRtc ds3231 pcf8563
```

Expected: script exits 0 and reports no forbidden RTC symbols in `esp32_core`.

- [ ] **Step 4: Build high-risk examples**

Run:

```bash
pio run -d examples/full_demo -e esp32_full
pio run -d examples/web_logs_ota -e esp32_full
pio run -d examples/app_events_demo -e esp32_full
```

Expected: all builds pass.

- [ ] **Step 5: Commit release-checklist updates if made**

If `scripts/check_trim_symbols.py` or `docs/09_release_checklist.md` changed:

```bash
git add scripts/check_trim_symbols.py docs/09_release_checklist.md
git commit -m "test: cover rtc trim checks"
```

If neither file changed, do not create an empty commit.

---

### Task 12: Hardware Validation Notes

**Files:**
- Modify: `docs/09_release_checklist.md`
- Modify: `docs/12_rtc_time_source_design.md` only if validation reveals a design correction.

- [ ] **Step 1: DS3231 present**

Flash `examples/rtc_time_source` DS3231 build to a board with DS3231 connected:

```bash
pio run -d examples/rtc_time_source -e esp32_ds3231 -t upload
```

Expected serial behavior:

```text
source=rtc rtc=ok
```

after RTC has been set once by NTP or `Esp32BaseRtc::setEpoch(...)`.

- [ ] **Step 2: DS3231 missing**

Flash the same DS3231 build to a board with no DS3231 connected.

Expected:

```text
rtc=missing
```

Device must continue booting and `Esp32Base::begin()` must not fail.

- [ ] **Step 3: PCF8563 present**

Flash PCF8563 build:

```bash
pio run -d examples/rtc_time_source -e esp32_pcf8563 -t upload
```

Expected serial behavior:

```text
source=rtc rtc=ok
```

after RTC has been set once.

- [ ] **Step 4: PCF8563 missing**

Flash the PCF8563 build to a board with no PCF8563 connected.

Expected:

```text
rtc=missing
```

Device must continue booting and `Esp32Base::begin()` must not fail.

- [ ] **Step 5: Record hardware validation gap**

Update `docs/09_release_checklist.md` with:

```markdown
- RTC release validation:
  - DS3231 present and missing cases checked.
  - PCF8563 present and missing cases checked.
  - NTP write-back checked on at least one RTC chip.
  - If hardware is unavailable, release notes must state RTC hardware validation was not completed.
```

- [ ] **Step 6: Commit hardware checklist update**

```bash
git add docs/09_release_checklist.md
git commit -m "docs: add rtc release validation checklist"
```

---

## Final Verification

- [ ] Run the native harnesses:

```bash
pio test -e native_time_harness
pio test -e native_time_pcf8563_harness
pio test -e native_web_harness
pio test -e native_config_harness
```

- [ ] Build representative examples:

```bash
pio run -d examples/basic -e esp32_core
pio run -d examples/basic -e esp32_net
pio run -d examples/basic -e esp32_full
pio run -d examples/rtc_time_source -e esp32_ds3231
pio run -d examples/rtc_time_source -e esp32_pcf8563
pio run -d examples/full_demo -e esp32_full
```

- [ ] Run whitespace and status checks:

```bash
git diff --check
git status --short --untracked-files=all
```

- [ ] If packaging is in scope for the branch, run:

```bash
platformio pkg pack .
python scripts/check_release_hygiene.py
```

Expected final state: all selected commands pass, worktree contains only intentional changes, and any skipped hardware validation is explicitly reported.
