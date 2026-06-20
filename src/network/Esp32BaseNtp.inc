#include "../Esp32BaseProfile.h"

#if ESP32BASE_ENABLE_NTP

#include "Esp32BaseNtp.h"
#include "../core/Esp32BaseConfig.h"
#include "../core/Esp32BaseLog.h"
#include "../runtime/Esp32BaseTime.h"
#include "../runtime/internal/Esp32BaseTimeInternal.h"
#if ESP32BASE_ENABLE_RTC
#include "../runtime/Esp32BaseRtc.h"
#endif

#include <esp_sntp.h>
#include <limits.h>
#include <time.h>

namespace {
bool g_started = false;
bool g_loggedSync = false;
char g_s1[48] = "pool.ntp.org";
char g_s2[48] = "time.nist.gov";
char g_s3[48] = "";
Esp32BaseNtp::TimeSyncCallback g_timeSyncCallback = nullptr;

bool trustedEpoch(uint32_t epoch) {
    return epoch >= ESP32BASE_NTP_SYNC_MIN_EPOCH;
}

bool sntpCompletedWithTrustedEpoch(uint32_t* epochSec) {
    const time_t now = time(nullptr);
    const bool synced = sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED &&
                        now >= static_cast<time_t>(ESP32BASE_NTP_SYNC_MIN_EPOCH);
    if (epochSec) {
        *epochSec = synced ? static_cast<uint32_t>(now) : 0;
    }
    return synced;
}

bool currentTrustedEpoch(uint32_t* epochSec) {
    const uint32_t now = static_cast<uint32_t>(time(nullptr));
    const bool trusted = trustedEpoch(now);
    if (epochSec) {
        *epochSec = trusted ? now : 0;
    }
    return trusted;
}

Esp32BaseNtp::TimeSnapshot ntpSnapshotFromBaseTime() {
    const Esp32BaseTime::Snapshot base = Esp32BaseTime::snapshot();
    Esp32BaseNtp::TimeSnapshot value = {};
    value.synced = base.synced && base.source == Esp32BaseTime::SOURCE_NTP;
    value.epochSec = value.synced ? base.epochSec : 0;
    value.uptimeSec = base.uptimeSec;
    value.bootId = base.bootId;
    value.bootStartEpochSec = value.synced ? base.bootStartEpochSec : 0;
    return value;
}

#if ESP32BASE_ENABLE_RTC && ESP32BASE_RTC_NTP_WRITEBACK
void maybeWriteBackRtc(uint32_t ntpEpochSec) {
    uint32_t rtcEpochSec = 0;
    bool writeBack = true;
    if (Esp32BaseRtc::readEpoch(&rtcEpochSec)) {
        const uint32_t diff = ntpEpochSec >= rtcEpochSec ? ntpEpochSec - rtcEpochSec : rtcEpochSec - ntpEpochSec;
        writeBack = diff > ESP32BASE_RTC_WRITEBACK_THRESHOLD_SEC;
    }
    if (!writeBack) {
        return;
    }
    if (Esp32BaseRtc::setEpoch(ntpEpochSec)) {
        Esp32BaseRtc::refresh();
        ESP32BASE_LOG_I("ntp", "rtc_writeback_ok driver=%s epoch=%lu",
                        Esp32BaseRtc::driverName(),
                        static_cast<unsigned long>(ntpEpochSec));
    } else {
        ESP32BASE_LOG_W("ntp", "rtc_writeback_failed driver=%s status=%s",
                        Esp32BaseRtc::driverName(),
                        Esp32BaseRtc::statusText());
    }
}
#endif
}

bool Esp32BaseNtp::initBootSession() {
    return Esp32BaseTime::initBootSession();
}

bool Esp32BaseNtp::begin() {
    initBootSession();
    configTime(ESP32BASE_NTP_GMT_OFFSET_SEC,
               ESP32BASE_NTP_DAYLIGHT_OFFSET_SEC,
               g_s1,
               g_s2[0] ? g_s2 : nullptr,
               g_s3[0] ? g_s3 : nullptr);
    g_started = true;
    g_loggedSync = false;
    ESP32BASE_LOG_I("ntp", "ntp_client_started timezone_offset=%ld daylight_offset=%ld servers=%s,%s,%s",
                    static_cast<long>(ESP32BASE_NTP_GMT_OFFSET_SEC),
                    static_cast<long>(ESP32BASE_NTP_DAYLIGHT_OFFSET_SEC),
                    g_s1, g_s2, g_s3);
    return true;
}

bool Esp32BaseNtp::isStarted() {
    return g_started;
}

bool Esp32BaseNtp::isTimeSynced() {
    uint32_t now = 0;
    const bool syncCompleted = sntpCompletedWithTrustedEpoch(&now);
    if (syncCompleted && !g_loggedSync) {
        if (!esp32base_internal::timeAcceptNtpEpoch(now)) {
            return false;
        }
        const Esp32BaseNtp::TimeSnapshot snap = ntpSnapshotFromBaseTime();
        char actual[32];
        formatTime(actual, sizeof(actual), "%Y-%m-%d %H:%M:%S");
        const uint32_t up = millis();
        const time_t bootWall = static_cast<time_t>(snap.bootStartEpochSec);
        struct tm bootTm;
        localtime_r(&bootWall, &bootTm);
        char boot[32];
        strftime(boot, sizeof(boot), "%Y-%m-%d %H:%M:%S", &bootTm);
        ESP32BASE_LOG_I("ntp", "time_synchronized actual_time=%s uptime_ms=%lu boot_time=%s", actual, static_cast<unsigned long>(up), boot);
        ESP32BASE_LOG_I("ntp", "time_mapping boot_millis=0 actual_time=%s current_millis=%lu current_time=%s", boot, static_cast<unsigned long>(up), actual);
        g_loggedSync = true;
        Esp32BaseLog::setTimeProvider(logTimeString);
        ESP32BASE_LOG_I("ntp", "log_timestamp_mode=absolute_datetime");
#if ESP32BASE_ENABLE_RTC && ESP32BASE_RTC_NTP_WRITEBACK
        maybeWriteBackRtc(now);
#endif
        if (g_timeSyncCallback) {
            g_timeSyncCallback(snap);
        }
    }
    if (g_loggedSync) {
        const Esp32BaseNtp::TimeSnapshot snap = ntpSnapshotFromBaseTime();
        return snap.synced && currentTrustedEpoch(nullptr);
    }
    return syncCompleted;
}

bool Esp32BaseNtp::isRealTime() {
    return isTimeSynced();
}

uint32_t Esp32BaseNtp::timestamp() {
    return static_cast<uint32_t>(time(nullptr));
}

Esp32BaseNtp::TimeSnapshot Esp32BaseNtp::snapshot() {
    isTimeSynced();
    return ntpSnapshotFromBaseTime();
}

void Esp32BaseNtp::onTimeSynced(TimeSyncCallback callback) {
    g_timeSyncCallback = callback;
    const bool alreadyNotified = g_loggedSync;
    if (g_timeSyncCallback && isTimeSynced() && alreadyNotified) {
        g_timeSyncCallback(snapshot());
    }
}

bool Esp32BaseNtp::isCurrentBootEvent(uint32_t bootId) {
    return bootId != 0 && bootId == Esp32BaseTime::snapshot().bootId;
}

bool Esp32BaseNtp::canResolveCurrentBootEvent(uint32_t bootId) {
    return isCurrentBootEvent(bootId) && snapshot().synced;
}

bool Esp32BaseNtp::resolveCurrentBootEvent(uint32_t bootId, uint32_t uptimeSec, uint32_t* epochSec) {
    if (!epochSec) {
        return false;
    }
    *epochSec = 0;
    if (!canResolveCurrentBootEvent(bootId)) {
        return false;
    }
    return Esp32BaseTime::resolveCurrentBootEvent(bootId, uptimeSec, epochSec);
}

void Esp32BaseNtp::setServers(const char* s1, const char* s2, const char* s3) {
    if (s1) strlcpy(g_s1, s1, sizeof(g_s1));
    if (s2) strlcpy(g_s2, s2, sizeof(g_s2));
    if (s3) strlcpy(g_s3, s3, sizeof(g_s3));
}

bool Esp32BaseNtp::formatTime(char* out, size_t len, const char* fmt) {
    if (!out || len == 0) {
        return false;
    }
    time_t now = time(nullptr);
    struct tm tmValue;
    localtime_r(&now, &tmValue);
    return strftime(out, len, fmt ? fmt : "%Y-%m-%d %H:%M:%S", &tmValue) > 0;
}

const char* Esp32BaseNtp::logTimeString() {
    static char buf[24];
    formatTime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S");
    return buf;
}

#endif
