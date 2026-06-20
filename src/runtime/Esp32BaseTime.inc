#include "../Esp32BaseProfile.h"

#if ESP32BASE_ENABLE_TIME

#include "Esp32BaseTime.h"
#include "internal/Esp32BaseTimeInternal.h"
#include "../core/Esp32BaseLog.h"
#include "../core/Esp32BaseSystem.h"

#include <esp_timer.h>
#include <stdio.h>
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

bool Esp32BaseTime::formatEpoch(uint32_t epochSec, char* out, size_t len, const char* fmt) {
    if (!out || len == 0) {
        return false;
    }
    if (!timeTrustedEpoch(epochSec)) {
        out[0] = '\0';
        return false;
    }
    const int64_t adjusted = static_cast<int64_t>(epochSec) +
                             static_cast<int64_t>(ESP32BASE_NTP_GMT_OFFSET_SEC) +
                             static_cast<int64_t>(ESP32BASE_NTP_DAYLIGHT_OFFSET_SEC);
    if (adjusted < 0) {
        out[0] = '\0';
        return false;
    }
    const time_t raw = static_cast<time_t>(adjusted);
    struct tm tmValue;
    gmtime_r(&raw, &tmValue);
    if (strftime(out, len, fmt ? fmt : "%Y-%m-%d %H:%M:%S", &tmValue) == 0) {
        out[0] = '\0';
        return false;
    }
    return true;
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
    return formatEpoch(s.epochSec, out, len, fmt);
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
