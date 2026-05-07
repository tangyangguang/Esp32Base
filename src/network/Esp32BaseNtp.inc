#include "../Esp32BaseProfile.h"

#if ESP32BASE_ENABLE_NTP

#include "Esp32BaseNtp.h"
#include "../core/Esp32BaseLog.h"

#include <time.h>

namespace {
bool g_started = false;
bool g_loggedSync = false;
uint32_t g_lastPendingLogMs = 0;
char g_s1[48] = "pool.ntp.org";
char g_s2[48] = "time.nist.gov";
char g_s3[48] = "";
}

bool Esp32BaseNtp::begin() {
    configTime(ESP32BASE_NTP_GMT_OFFSET_SEC,
               ESP32BASE_NTP_DAYLIGHT_OFFSET_SEC,
               g_s1,
               g_s2[0] ? g_s2 : nullptr,
               g_s3[0] ? g_s3 : nullptr);
    g_started = true;
    g_loggedSync = false;
    g_lastPendingLogMs = 0;
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
    time_t now = time(nullptr);
    const bool synced = now >= static_cast<time_t>(ESP32BASE_NTP_SYNC_MIN_EPOCH);
    if (synced && !g_loggedSync) {
        char actual[32];
        formatTime(actual, sizeof(actual), "%Y-%m-%d %H:%M:%S");
        const uint32_t up = millis();
        const time_t bootWall = now - static_cast<time_t>(up / 1000U);
        struct tm bootTm;
        localtime_r(&bootWall, &bootTm);
        char boot[32];
        strftime(boot, sizeof(boot), "%Y-%m-%d %H:%M:%S", &bootTm);
        ESP32BASE_LOG_I("ntp", "time_synchronized actual_time=%s uptime_ms=%lu boot_time=%s", actual, static_cast<unsigned long>(up), boot);
        ESP32BASE_LOG_I("ntp", "time_mapping boot_millis=0 actual_time=%s current_millis=%lu current_time=%s", boot, static_cast<unsigned long>(up), actual);
        g_loggedSync = true;
        Esp32BaseLog::setTimeProvider(logTimeString);
        ESP32BASE_LOG_I("ntp", "log_timestamp_mode=absolute_datetime");
    } else if (!synced && (g_lastPendingLogMs == 0 || millis() - g_lastPendingLogMs >= 30000UL)) {
        g_lastPendingLogMs = millis();
        ESP32BASE_LOG_W("ntp", "ntp_sync_pending elapsed=%lu s raw_time=%lu", static_cast<unsigned long>(millis() / 1000UL), static_cast<unsigned long>(now));
    }
    return synced;
}

uint32_t Esp32BaseNtp::timestamp() {
    return static_cast<uint32_t>(time(nullptr));
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
