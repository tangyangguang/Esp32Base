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

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_time_defaults_to_uptime_without_real_time);
    RUN_TEST(test_rtc_time_establishes_boot_mapping);
    RUN_TEST(test_ntp_overrides_rtc_time);
    RUN_TEST(test_resolve_current_boot_event_uses_active_mapping);
    RUN_TEST(test_rtc_defaults_to_selected_driver_address_when_configured_address_zero);
    RUN_TEST(test_rtc_missing_is_nonfatal_status);
    return UNITY_END();
}
