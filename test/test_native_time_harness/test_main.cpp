#include <unity.h>
#include <Wire.h>

#include "runtime/Esp32BaseTime.h"
#include "runtime/internal/Esp32BaseTimeInternal.h"
#include "runtime/Esp32BaseTime.inc"
#include "runtime/Esp32BaseRtc.h"
#include "runtime/Esp32BaseRtc.inc"

uint32_t g_nativeMillis = 0;
int64_t g_nativeEspTimerUs = 0;
NativeSerial Serial;
TwoWire Wire;

static void resetTimeHarness() {
    g_nativeMillis = 0;
    g_nativeEspTimerUs = 0;
    esp32base_internal::timeNativeReset();
}

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

void test_time_format_uses_configured_offset() {
    resetTimeHarness();
    g_nativeEspTimerUs = 5LL * 1000000LL;

    TEST_ASSERT_TRUE(Esp32BaseTime::initBootSession());
    TEST_ASSERT_TRUE(esp32base_internal::timeAcceptRtcEpoch(1700000105UL));
    char text[32];
    TEST_ASSERT_TRUE(Esp32BaseTime::formatTime(text, sizeof(text), "%Y-%m-%d %H:%M:%S"));
    TEST_ASSERT_EQUAL_STRING("2023-11-15 06:15:05", text);
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

void test_rtc_defaults_to_selected_driver_address_when_configured_address_zero() {
    resetTimeHarness();
    Wire.devices.clear();

    TEST_ASSERT_TRUE(Esp32BaseRtc::configure(Wire, 0));
    TEST_ASSERT_FALSE(Esp32BaseRtc::isAvailable());
#if ESP32BASE_RTC_DRIVER == ESP32BASE_RTC_DRIVER_PCF8563
    TEST_ASSERT_EQUAL_STRING("pcf8563", Esp32BaseRtc::driverName());
#else
    TEST_ASSERT_EQUAL_STRING("ds3231", Esp32BaseRtc::driverName());
#endif
}

void test_rtc_missing_is_nonfatal_status() {
    resetTimeHarness();
    Wire.devices.clear();

    TEST_ASSERT_TRUE(Esp32BaseRtc::configure(Wire, 0));
    TEST_ASSERT_TRUE(Esp32BaseRtc::begin());
    TEST_ASSERT_FALSE(Esp32BaseRtc::isAvailable());
    TEST_ASSERT_EQUAL(Esp32BaseRtc::STATUS_MISSING, Esp32BaseRtc::status());
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

void test_ds3231_reads_12_hour_pm_when_selected() {
#if ESP32BASE_RTC_DRIVER == ESP32BASE_RTC_DRIVER_DS3231
    resetTimeHarness();
    Wire.devices.clear();
    loadDs3231Time();
    Wire.devices[0x68][0x02] = static_cast<uint8_t>(0x40 | 0x20 | bcd(3));

    TEST_ASSERT_TRUE(Esp32BaseRtc::configure(Wire, 0x68));
    uint32_t epoch = 0;
    TEST_ASSERT_TRUE(Esp32BaseRtc::readEpoch(&epoch));
    TEST_ASSERT_EQUAL(Esp32BaseRtc::STATUS_OK, Esp32BaseRtc::status());
    TEST_ASSERT_EQUAL_UINT32(1704207845UL, epoch);
#endif
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

void test_rtc_rejects_impossible_calendar_date() {
    resetTimeHarness();
    Wire.devices.clear();
#if ESP32BASE_RTC_DRIVER == ESP32BASE_RTC_DRIVER_DS3231
    loadDs3231Time();
    Wire.devices[0x68][0x04] = bcd(31);
    Wire.devices[0x68][0x05] = bcd(2);
    TEST_ASSERT_TRUE(Esp32BaseRtc::configure(Wire, 0x68));
#elif ESP32BASE_RTC_DRIVER == ESP32BASE_RTC_DRIVER_PCF8563
    loadPcf8563Time();
    Wire.devices[0x51][0x05] = bcd(31);
    Wire.devices[0x51][0x07] = bcd(2);
    TEST_ASSERT_TRUE(Esp32BaseRtc::configure(Wire, 0x51));
#else
    TEST_FAIL_MESSAGE("Unsupported RTC driver in native time harness");
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
#elif ESP32BASE_RTC_DRIVER == ESP32BASE_RTC_DRIVER_PCF8563
    loadPcf8563Time();
    TEST_ASSERT_TRUE(Esp32BaseRtc::configure(Wire, 0x51));
#else
    TEST_FAIL_MESSAGE("Unsupported RTC driver in native time harness");
#endif
    TEST_ASSERT_TRUE(Esp32BaseRtc::setEpoch(1709251199UL));

    uint32_t epoch = 0;
    TEST_ASSERT_TRUE(Esp32BaseRtc::readEpoch(&epoch));
    TEST_ASSERT_EQUAL_UINT32(1709251199UL, epoch);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_time_defaults_to_uptime_without_real_time);
    RUN_TEST(test_rtc_time_establishes_boot_mapping);
    RUN_TEST(test_time_format_uses_configured_offset);
    RUN_TEST(test_ntp_overrides_rtc_time);
    RUN_TEST(test_resolve_current_boot_event_uses_active_mapping);
    RUN_TEST(test_rtc_defaults_to_selected_driver_address_when_configured_address_zero);
    RUN_TEST(test_rtc_missing_is_nonfatal_status);
    RUN_TEST(test_ds3231_reads_valid_epoch);
    RUN_TEST(test_ds3231_osf_marks_clock_stopped);
    RUN_TEST(test_ds3231_reads_12_hour_pm_when_selected);
    RUN_TEST(test_pcf8563_reads_valid_epoch_when_selected);
    RUN_TEST(test_pcf8563_vl_marks_clock_stopped_when_selected);
    RUN_TEST(test_rtc_rejects_impossible_calendar_date);
    RUN_TEST(test_rtc_set_epoch_round_trips_selected_driver);
    return UNITY_END();
}
