# RTC Time Source Design

Date: 2026-06-20

## Summary

Esp32Base will add an optional RTC-backed trusted time source for devices that must keep real wall-clock time after power loss and offline startup. The first implementation supports DS3231 and PCF8563 through explicit compile-time configuration. It does not auto-detect RTC chips.

The feature is a time-source integration, not a full RTC peripheral framework. Esp32Base owns only the system time semantics: trusted epoch, boot time mapping, logs, App Events, Web Status, and NTP write-back. Application projects keep direct control of chip-specific peripheral features such as alarms, INT/SQW pins, timer outputs, temperature reads, and calibration.

## Goals

- Provide real time after power loss for boards with a battery-backed RTC.
- Keep RTC support optional and trimmed out by default.
- Support DS3231 and PCF8563 in the first version.
- Select the RTC chip by project configuration, not runtime probing.
- Keep common time behavior independent of the chosen RTC chip.
- Let devices boot normally when the configured RTC is missing, invalid, not powered by battery, or not connected.
- Use NTP as the highest-trust source when available, write NTP time back to RTC, and use RTC on later offline boots.
- Update all internal Esp32Base consumers that currently assume NTP is the only real-time source.

## Non-Goals

- No runtime auto-detection or I2C bus scan.
- No DS1307 support in the first version.
- No RTC alarm API.
- No INT/SQW GPIO ownership.
- No square-wave, 32 kHz output, countdown timer, temperature, or calibration API.
- No multi-user time policy, timezone UI, or cloud time integration.
- No change to profile count. The existing seven profiles remain fixed.

## External References

- DS3231: Analog Devices DS3231 temperature-compensated I2C RTC.
  https://www.analog.com/en/products/ds3231.html
- PCF8563: NXP PCF8563 low-power I2C RTC/calendar.
  https://www.nxp.com/products/analog-and-mixed-signal/real-time-clocks/real-time-clock-calendar:PCF8563
- ESP-IDF System Time: system time, RTC timer, and SNTP concepts.
  https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/system_time.html

## Architecture

Add a small common time coordinator named `Esp32BaseTime`, and an optional RTC module, `Esp32BaseRtc`.

```text
Application
  ↓
Esp32Base
  ↓
Network: Esp32BaseNtp
  ↓
Runtime: Esp32BaseTime, Esp32BaseRtc
  ↓
Core: Log, Config, System
```

Dependency direction must stay valid:

- `Esp32BaseTime` depends only on Core/system time primitives.
- `Esp32BaseRtc` depends on `Esp32BaseTime`, Core Log, and Arduino `Wire`.
- `Esp32BaseNtp` remains Network and may update `Esp32BaseTime` and optionally `Esp32BaseRtc`.
- Web and App Events read time through `Esp32BaseTime`, not directly through `Esp32BaseNtp`.

`Esp32BaseTime` becomes the public source of truth for trusted time. `Esp32BaseNtp` remains the NTP client API and may keep compatibility helpers, but base-library internals should stop treating NTP as the only source of real time.

## Time Authority

Authoritative time order:

```text
NTP > RTC > uptime
```

Meaning:

- NTP is trusted when SNTP completed and the epoch passes the configured minimum trusted epoch.
- RTC is trusted when the configured chip responds, its clock integrity flag is valid, and its calendar fields convert to a plausible epoch.
- Uptime is always available but is not real wall-clock time.

When RTC or NTP provides trusted epoch, `Esp32BaseTime` records:

- `source`: `ntp`, `rtc`, or `uptime`.
- `epochSec`: current trusted Unix epoch when real time is available.
- `uptimeSec`: current uptime from ESP-IDF monotonic timer.
- `bootId`: existing system boot count semantics.
- `bootStartEpochSec`: `epochSec - uptimeSec`, when real time is trusted.

`Esp32BaseTime` should also update ESP32 system time when accepting RTC time so `time()` and existing formatting paths remain coherent.

## RTC Storage Convention

RTC chips store calendar fields and do not know timezone. Esp32Base must document one convention and use it consistently.

Design decision: store RTC calendar fields as UTC wall-clock fields derived from Unix epoch. Display remains controlled by the existing Esp32Base time formatting rules. This avoids DST/local-time ambiguity and lets DS3231 and PCF8563 share the same driver contract.

If the current NTP implementation needs cleanup to preserve true Unix epoch semantics, that is part of the implementation plan. The public contract must be epoch-based, not chip-local-time-based.

## Configuration

RTC support is disabled by default.

```cpp
#define ESP32BASE_ENABLE_TIME 1
#define ESP32BASE_ENABLE_RTC 1

#define ESP32BASE_RTC_DRIVER_DS3231 1
#define ESP32BASE_RTC_DRIVER_PCF8563 2
#define ESP32BASE_RTC_DRIVER ESP32BASE_RTC_DRIVER_DS3231
```

Defaults:

- `ESP32BASE_ENABLE_TIME=1` automatically when `ESP32BASE_ENABLE_NTP=1` or `ESP32BASE_ENABLE_RTC=1`; otherwise `0`.
- `ESP32BASE_ENABLE_RTC=0`.
- If RTC is enabled and no driver is selected, default to DS3231.
- DS3231 default I2C address: `0x68`.
- PCF8563 default I2C address: `0x51`.
- `ESP32BASE_RTC_AUTO_WIRE_BEGIN=0`.
- `ESP32BASE_RTC_NTP_WRITEBACK=1`.
- `ESP32BASE_RTC_WRITEBACK_THRESHOLD_SEC=2`.
- `ESP32BASE_RTC_STATUS_REFRESH_MS=0`, meaning no periodic RTC polling by default.

Optional board configuration:

```cpp
#define ESP32BASE_RTC_I2C_ADDR 0x68
#define ESP32BASE_RTC_AUTO_WIRE_BEGIN 1
#define ESP32BASE_RTC_SDA 21
#define ESP32BASE_RTC_SCL 22
#define ESP32BASE_RTC_I2C_CLOCK_HZ 100000
```

I2C ownership rule:

- By default, application code initializes `Wire`.
- Esp32Base only calls `Wire.begin(...)` when `ESP32BASE_RTC_AUTO_WIRE_BEGIN=1`.
- Esp32Base does not scan the bus and does not touch unrelated I2C devices.

## Public API

Proposed common time API:

```cpp
class Esp32BaseTime {
public:
    enum Source : uint8_t {
        SOURCE_UPTIME,
        SOURCE_RTC,
        SOURCE_NTP
    };

    struct Snapshot {
        bool synced;
        Source source;
        uint32_t epochSec;
        uint32_t uptimeSec;
        uint32_t bootId;
        uint32_t bootStartEpochSec;
    };

    using TimeSyncCallback = void (*)(const Snapshot& snapshot);

    static bool initBootSession();
    static Snapshot snapshot();
    static bool isRealTime();
    static bool formatTime(char* out, size_t len, const char* fmt);
    static bool resolveCurrentBootEvent(uint32_t bootId, uint32_t uptimeSec, uint32_t* epochSec);
    static void onTimeSynced(TimeSyncCallback callback);
    static const char* sourceName(Source source);
};
```

Proposed RTC API:

```cpp
class Esp32BaseRtc {
public:
    enum Driver : uint8_t {
        DRIVER_DS3231,
        DRIVER_PCF8563
    };

    enum Status : uint8_t {
        STATUS_DISABLED,
        STATUS_NOT_STARTED,
        STATUS_OK,
        STATUS_MISSING,
        STATUS_I2C_ERROR,
        STATUS_TIME_INVALID,
        STATUS_CLOCK_STOPPED,
        STATUS_CONFIG_ERROR
    };

    static bool begin();
    static void handle();
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

Existing `Esp32BaseNtp::snapshot()` will remain as a compatibility helper when NTP is enabled and delegate to `Esp32BaseTime::snapshot()`. New code should use `Esp32BaseTime::snapshot()` directly.

## Driver Contract

The common RTC layer sees each chip through a small driver contract:

```cpp
struct Esp32BaseRtcDriverOps {
    const char* name;
    uint8_t defaultAddress;
    bool (*probe)(TwoWire& wire, uint8_t address);
    bool (*readEpoch)(TwoWire& wire, uint8_t address, uint32_t* epoch, Esp32BaseRtc::Status* status);
    bool (*writeEpoch)(TwoWire& wire, uint8_t address, uint32_t epoch, Esp32BaseRtc::Status* status);
};
```

The implementation can use static functions selected by preprocessor instead of virtual classes. This keeps code size predictable and avoids dynamic allocation.

Chip-specific mapping:

- DS3231 maps OSF/oscillator-stop state to `STATUS_CLOCK_STOPPED`.
- PCF8563 maps voltage-low / clock integrity status to `STATUS_CLOCK_STOPPED` or `STATUS_TIME_INVALID`.
- Invalid BCD values, impossible dates, and epochs below the trusted minimum map to `STATUS_TIME_INVALID`.
- I2C NACK or incomplete transfers map to `STATUS_MISSING` or `STATUS_I2C_ERROR`.

## Runtime Behavior

Startup:

1. Core Config and System initialize.
2. `Esp32BaseTime::initBootSession()` initializes boot id and uptime mapping.
3. If RTC is enabled, `Esp32BaseRtc::begin()` runs after System and before App Events time provider setup.
4. If RTC returns trusted epoch, `Esp32BaseTime` accepts it as `SOURCE_RTC`, sets the log time provider, and makes real time available before WiFi/NTP.
5. If RTC fails or is invalid, startup continues with uptime.

Main loop:

- RTC must not be polled every loop.
- RTC background polling is disabled by default with `ESP32BASE_RTC_STATUS_REFRESH_MS=0`.
- Projects that need periodic RTC health refresh may set `ESP32BASE_RTC_STATUS_REFRESH_MS` to a low-frequency interval, such as 30000-60000 ms.
- `Esp32BaseTime::snapshot()` uses cached boot mapping and monotonic uptime, not an I2C read.
- Web Status may use cached RTC state or perform one bounded read when rendering diagnostics.

NTP sync:

1. `Esp32BaseNtp` starts only when WiFi is connected, as it does now.
2. When NTP reaches trusted sync, it updates `Esp32BaseTime` as `SOURCE_NTP`.
3. If RTC is enabled and write-back is enabled, compare NTP epoch and RTC epoch.
4. Write RTC only when absolute difference is greater than `ESP32BASE_RTC_WRITEBACK_THRESHOLD_SEC`.
5. NTP must not overwrite RTC alarm, INT/SQW, timer, or unrelated status bits.

Failure handling:

- RTC failure never fails `Esp32Base::begin()`.
- Repeated RTC read/write failures should log at WARN only on state transitions or bounded intervals.
- Missing RTC on a firmware built with RTC enabled is a diagnostic state, not a fatal state.
- If RTC becomes invalid after earlier success, current boot time mapping remains usable only if it was already established from trusted time; Status should show RTC degraded.

## Application Boundary

Application projects should use Esp32Base for system time:

- current trusted time;
- event timestamps;
- checking whether time is real or uptime-only;
- setting RTC time from a maintenance workflow;
- reading RTC availability and health.

Application projects may directly use `Wire` or another RTC library for chip peripheral features:

- DS3231 alarm1/alarm2;
- PCF8563 alarm/timer;
- INT/SQW pin wiring;
- wakeup GPIO logic;
- square wave / clock output;
- temperature reads;
- aging or offset calibration.

Conflict rules:

- Esp32Base owns time registers only when reading/writing current time.
- Esp32Base must not configure alarm, timer, SQW, CLKOUT, interrupt-enable, or calibration features.
- Esp32Base may clear only the clock-integrity flag it intentionally handles, and must preserve unrelated status bits.
- Application ISR code must not perform I2C transactions directly; use ISR flags and handle I2C in loop/task context.
- If application code writes RTC time itself, Esp32Base should expose a refresh path or naturally pick up the new time on the next RTC check.

## Internal Impact Audit

The following areas currently depend on NTP as the only real-time source and must be changed or reviewed.

### `src/Esp32Base.cpp`

- Replace `appEventLogTimeFromNtp()` with a provider backed by `Esp32BaseTime::snapshot()`.
- Initialize `Esp32BaseTime` before RTC, NTP, App Events, and FileLog.
- Initialize RTC when `ESP32BASE_ENABLE_RTC=1`.
- Keep NTP deferred start on WiFi connected, but route trusted sync into `Esp32BaseTime`.
- Add RTC `handle()` only for low-frequency maintenance.

### `src/network/Esp32BaseNtp.*`

- Keep NTP client responsibilities: server configuration, SNTP start, trusted sync detection.
- Move boot-session and current-boot event resolution semantics to `Esp32BaseTime`, or delegate to it.
- On trusted sync, update `Esp32BaseTime`, set log timestamp mode through Time, and trigger RTC write-back.
- Preserve or document migration for existing `Esp32BaseNtp::snapshot()` callers.

### `src/core/Esp32BaseLog.*`

- Keep the generic time-provider hook.
- Provider should become `Esp32BaseTime::logTimeString` or equivalent.
- Logs before RTC/NTP use uptime; logs after RTC or NTP accepted use absolute time.

### `src/runtime/Esp32BaseAppEventLog.*`

- Time provider should use `Esp32BaseTime`.
- `FLAG_TIME_SYNCED` remains meaningful but should mean "trusted real time", not specifically NTP.
- Stored `epochSec`, `bootId`, and `uptimeSec` remain unchanged.

### `src/web/internal/WebStatus.cpp`

- Rename or supplement the current "NTP time" row so it represents general time state.
- Show time source: `uptime`, `rtc`, or `ntp`.
- Show RTC driver and status when RTC is enabled.
- `currentWatchdogTripResetTime()` must use `Esp32BaseTime::snapshot()`, not NTP directly.

### `src/web/internal/WebAppEvents.cpp`

- Resolve current-boot events through `Esp32BaseTime::resolveCurrentBootEvent()`.
- UI text should say "trusted real time" rather than "NTP" where applicable.

### `src/web/internal/WebFs.cpp`

- File modified time display already checks trusted minimum epoch. Review labels so they do not imply NTP-only trust.

### Documentation

Update:

- `README.md`: feature summary, profile table notes, application guidance.
- `docs/01_architecture.md`: add Time and RTC modules while preserving dependency direction.
- `docs/02_profiles.md`: add macros and dependency rules.
- `docs/03_api.md`: add `Esp32BaseTime` and `Esp32BaseRtc`; update NTP time wording.
- `docs/04_web.md`: update Status and App Events wording.
- `docs/07_diagnostics.md`: update log timestamp and event time semantics.
- `docs/08_arduino_core_compat.md`: document Wire/I2C and system time assumptions if needed.
- `docs/10_known_limitations.md`: note no RTC auto-detect and no alarm/SQW ownership.

### Examples and Tests

Add or update:

- A small RTC example showing DS3231 and PCF8563 configuration variants.
- Native harness tests for time authority selection, event resolution, and NTP write-back decision logic.
- Compile checks for RTC disabled, DS3231 selected, and PCF8563 selected.
- Trim-symbol checks ensuring RTC code is absent when disabled.
- Documentation examples showing `Wire.begin()` ownership and `ESP32BASE_RTC_AUTO_WIRE_BEGIN`.

Hardware validation:

- DS3231 board present and valid time.
- DS3231 missing while firmware enables DS3231.
- DS3231 OSF/clock stopped condition.
- PCF8563 board present and valid time.
- PCF8563 missing while firmware enables PCF8563.
- PCF8563 low-voltage/clock integrity invalid condition.
- NTP write-back to both chips.

## Acceptance Criteria

- RTC disabled builds behave as before except for documented Time facade additions.
- RTC-enabled firmware boots successfully when the configured RTC chip is absent.
- DS3231 can provide trusted time on offline startup after prior setting.
- PCF8563 can provide trusted time on offline startup after prior setting.
- NTP overrides RTC when available and writes back to RTC only when drift exceeds threshold.
- App Events record trusted `epochSec` when time comes from RTC or NTP.
- App Events still record `bootId + uptimeSec` when no trusted real time exists.
- Logs switch to absolute timestamps after RTC or NTP establishes trusted time.
- Web Status reports time source and RTC status clearly.
- Business code can still use RTC alarms/INT/SQW directly without Esp32Base overwriting those settings.

## Implementation Decisions

- Confirm the current Arduino ESP32 `configTime` / `time()` behavior and normalize all internal contracts to real Unix epoch seconds.
- Keep `Esp32BaseNtp::snapshot()` as a delegating compatibility API.
- Keep RTC status refresh on demand by default; periodic refresh is opt-in through `ESP32BASE_RTC_STATUS_REFRESH_MS`.
