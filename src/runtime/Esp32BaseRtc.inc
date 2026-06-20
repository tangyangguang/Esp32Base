#include "../Esp32BaseProfile.h"

#if ESP32BASE_ENABLE_RTC

#include "Esp32BaseRtc.h"
#include "Esp32BaseTime.h"
#include "internal/Esp32BaseTimeInternal.h"
#include "internal/Esp32BaseRtcDrivers.h"

extern TwoWire Wire;

namespace {
TwoWire* g_rtcWire = &Wire;
uint8_t g_rtcAddress = ESP32BASE_RTC_I2C_ADDR;
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

#if ESP32BASE_RTC_DRIVER == ESP32BASE_RTC_DRIVER_PCF8563
const Esp32BaseRtcDriverOps kRtcMissingOps = {
    "pcf8563",
    0x51,
    rtcStubProbe,
    rtcStubRead,
    rtcStubWrite
};
#else
const Esp32BaseRtcDriverOps kRtcMissingOps = {
    "ds3231",
    0x68,
    rtcStubProbe,
    rtcStubRead,
    rtcStubWrite
};
#endif

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
#if ESP32BASE_RTC_SDA >= 0 && ESP32BASE_RTC_SCL >= 0
    g_rtcWire->begin(ESP32BASE_RTC_SDA, ESP32BASE_RTC_SCL, ESP32BASE_RTC_I2C_CLOCK_HZ);
#else
    g_rtcWire->begin();
#endif
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
    Status driverStatus = STATUS_OK;
    if (!ops->readEpoch(*g_rtcWire, g_rtcAddress, epochSec, &driverStatus)) {
        g_rtcStatus = driverStatus;
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
    Status driverStatus = STATUS_OK;
    const bool ok = ops->writeEpoch(*g_rtcWire, g_rtcAddress ? g_rtcAddress : ops->defaultAddress, epochSec, &driverStatus);
    g_rtcStatus = ok ? STATUS_OK : driverStatus;
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

namespace {
const Esp32BaseRtcDriverOps* rtcDriverOps() {
    return &kRtcMissingOps;
}
}

#endif
