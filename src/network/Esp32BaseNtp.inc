#include "../Esp32BaseProfile.h"

#if ESP32BASE_ENABLE_NTP

#include "Esp32BaseNtp.h"
#include "../core/Esp32BaseConfig.h"
#include "../core/Esp32BaseLog.h"

#include <esp_sntp.h>
#include <limits.h>
#include <time.h>

namespace {
bool g_started = false;
bool g_loggedSync = false;
bool g_bootSessionReady = false;
char g_s1[48] = "pool.ntp.org";
char g_s2[48] = "time.nist.gov";
char g_s3[48] = "";
uint32_t g_bootId = 0;
uint32_t g_bootStartEpochSec = 0;
Esp32BaseNtp::TimeSyncCallback g_timeSyncCallback = nullptr;

uint32_t currentUptimeSec() {
    return millis() / 1000U;
}

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
}

bool Esp32BaseNtp::initBootSession() {
    if (g_bootSessionReady) {
        return true;
    }
    const int32_t stored = Esp32BaseConfig::getInt("eb_sys", "time_boot_id", 0);
    const uint32_t previous = stored > 0 ? static_cast<uint32_t>(stored) : 0;
    g_bootId = previous >= static_cast<uint32_t>(INT32_MAX) ? 1U : previous + 1U;
    const bool ok = Esp32BaseConfig::setInt("eb_sys", "time_boot_id", static_cast<int32_t>(g_bootId));
    g_bootSessionReady = true;
    if (ok) {
        ESP32BASE_LOG_I("ntp", "time_boot_session boot_id=%lu", static_cast<unsigned long>(g_bootId));
    } else {
        ESP32BASE_LOG_W("ntp", "time_boot_session_persist_failed stored=%ld", static_cast<long>(stored));
    }
    return ok;
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
    const bool synced = sntpCompletedWithTrustedEpoch(&now);
    if (synced && !g_loggedSync) {
        if (!g_bootSessionReady) {
            initBootSession();
        }
        const uint32_t upSec = currentUptimeSec();
        g_bootStartEpochSec = now >= upSec ? now - upSec : 0;
        char actual[32];
        formatTime(actual, sizeof(actual), "%Y-%m-%d %H:%M:%S");
        const uint32_t up = millis();
        const time_t bootWall = static_cast<time_t>(g_bootStartEpochSec);
        struct tm bootTm;
        localtime_r(&bootWall, &bootTm);
        char boot[32];
        strftime(boot, sizeof(boot), "%Y-%m-%d %H:%M:%S", &bootTm);
        ESP32BASE_LOG_I("ntp", "time_synchronized actual_time=%s uptime_ms=%lu boot_time=%s", actual, static_cast<unsigned long>(up), boot);
        ESP32BASE_LOG_I("ntp", "time_mapping boot_millis=0 actual_time=%s current_millis=%lu current_time=%s", boot, static_cast<unsigned long>(up), actual);
        g_loggedSync = true;
        Esp32BaseLog::setTimeProvider(logTimeString);
        ESP32BASE_LOG_I("ntp", "log_timestamp_mode=absolute_datetime");
        if (g_timeSyncCallback) {
            g_timeSyncCallback(snapshot());
        }
    }
    return synced;
}

bool Esp32BaseNtp::isRealTime() {
    return isTimeSynced();
}

uint32_t Esp32BaseNtp::timestamp() {
    return static_cast<uint32_t>(time(nullptr));
}

Esp32BaseNtp::TimeSnapshot Esp32BaseNtp::snapshot() {
    TimeSnapshot value = {};
    value.uptimeSec = currentUptimeSec();
    value.bootId = g_bootId;
    value.bootStartEpochSec = g_bootStartEpochSec;
    value.synced = isTimeSynced();
    value.epochSec = value.synced ? timestamp() : 0;
    if (value.synced && value.bootStartEpochSec == 0 && value.epochSec >= value.uptimeSec) {
        value.bootStartEpochSec = value.epochSec - value.uptimeSec;
    }
    return value;
}

void Esp32BaseNtp::onTimeSynced(TimeSyncCallback callback) {
    g_timeSyncCallback = callback;
    const bool alreadyNotified = g_loggedSync;
    if (g_timeSyncCallback && isTimeSynced() && alreadyNotified) {
        g_timeSyncCallback(snapshot());
    }
}

bool Esp32BaseNtp::isCurrentBootEvent(uint32_t bootId) {
    return bootId != 0 && bootId == g_bootId;
}

bool Esp32BaseNtp::canResolveCurrentBootEvent(uint32_t bootId) {
    return isCurrentBootEvent(bootId) && snapshot().synced && g_bootStartEpochSec != 0;
}

bool Esp32BaseNtp::resolveCurrentBootEvent(uint32_t bootId, uint32_t uptimeSec, uint32_t* epochSec) {
    if (!epochSec) {
        return false;
    }
    *epochSec = 0;
    if (!canResolveCurrentBootEvent(bootId)) {
        return false;
    }
    *epochSec = g_bootStartEpochSec + uptimeSec;
    return trustedEpoch(*epochSec);
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
