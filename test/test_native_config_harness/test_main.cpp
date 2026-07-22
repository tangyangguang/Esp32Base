#include <unity.h>

#include "core/Esp32BaseConfig.h"
#include "core/Esp32BaseLog.h"
#include "core/internal/Esp32BaseConfigInternal.h"
#include "network/internal/Esp32BaseRecoveryButton.h"
#include "Preferences.h"

uint32_t g_nativeMillis = 0;
NativeSerial Serial;

void test_uptime_formatter_supports_more_than_32_bit_millis() {
    char value[32] = "";
    Esp32BaseLog::formatUptime64(50ULL * 24ULL * 60ULL * 60ULL * 1000ULL + 3723004ULL,
                                 value,
                                 sizeof(value));
    TEST_ASSERT_EQUAL_STRING("50d 01:02:03", value);
}

static void resetConfigHarness() {
    native_nvs::reset();
    g_nativeMillis = 0;
    Esp32BaseConfig::resumeDeferredFlush();
    Esp32BaseConfig::enableConfigAudit(false);
    Esp32BaseConfig::enableConfigReadAudit(false);
    Esp32BaseConfig::clearNamespace("app_cfg");
    TEST_ASSERT_EQUAL_UINT8(0, Esp32BaseConfig::pendingCount());
}

void test_recovery_button_triggers_at_threshold_once_per_press() {
    esp32base_internal::RecoveryButtonTracker tracker;
    tracker.reset(0);
    TEST_ASSERT_FALSE(tracker.update(true, 0, 50, 10000));
    TEST_ASSERT_FALSE(tracker.update(true, 49, 50, 10000));
    TEST_ASSERT_FALSE(tracker.update(true, 50, 50, 10000));
    TEST_ASSERT_FALSE(tracker.update(true, 10049, 50, 10000));
    TEST_ASSERT_TRUE(tracker.update(true, 10050, 50, 10000));
    TEST_ASSERT_FALSE(tracker.update(true, 20000, 50, 10000));
}

void test_recovery_button_release_and_bounce_restart_hold_window() {
    esp32base_internal::RecoveryButtonTracker tracker;
    tracker.reset(0);
    TEST_ASSERT_FALSE(tracker.update(true, 0, 50, 10000));
    TEST_ASSERT_FALSE(tracker.update(false, 20, 50, 10000));
    TEST_ASSERT_FALSE(tracker.update(true, 40, 50, 10000));
    TEST_ASSERT_FALSE(tracker.update(true, 90, 50, 10000));
    TEST_ASSERT_FALSE(tracker.update(true, 10089, 50, 10000));
    TEST_ASSERT_TRUE(tracker.update(true, 10090, 50, 10000));
    TEST_ASSERT_FALSE(tracker.update(false, 10100, 50, 10000));
    TEST_ASSERT_FALSE(tracker.update(false, 10150, 50, 10000));
    TEST_ASSERT_FALSE(tracker.update(true, 10200, 50, 10000));
    TEST_ASSERT_FALSE(tracker.update(true, 10250, 50, 10000));
    TEST_ASSERT_TRUE(tracker.update(true, 20250, 50, 10000));
}

void test_deferred_int_skips_when_nvs_already_has_same_value() {
    resetConfigHarness();

    TEST_ASSERT_TRUE(Esp32BaseConfig::setInt("app_cfg", "counter", 42));
    TEST_ASSERT_EQUAL_UINT(1, native_nvs::writeCount());

    TEST_ASSERT_TRUE(Esp32BaseConfig::setIntDeferred("app_cfg", "counter", 42, 1000));

    TEST_ASSERT_EQUAL_UINT8(0, Esp32BaseConfig::pendingCount());
    TEST_ASSERT_TRUE(Esp32BaseConfig::flushAll());
    TEST_ASSERT_EQUAL_UINT(1, native_nvs::writeCount());
}

void test_deferred_bool_skips_when_nvs_already_has_same_value() {
    resetConfigHarness();

    TEST_ASSERT_TRUE(Esp32BaseConfig::setBool("app_cfg", "enabled", true));
    TEST_ASSERT_EQUAL_UINT(1, native_nvs::writeCount());

    TEST_ASSERT_TRUE(Esp32BaseConfig::setBoolDeferred("app_cfg", "enabled", true, 1000));

    TEST_ASSERT_EQUAL_UINT8(0, Esp32BaseConfig::pendingCount());
    TEST_ASSERT_TRUE(Esp32BaseConfig::flushAll());
    TEST_ASSERT_EQUAL_UINT(1, native_nvs::writeCount());
}

void test_deferred_string_skips_when_nvs_already_has_same_value() {
    resetConfigHarness();

    TEST_ASSERT_TRUE(Esp32BaseConfig::setStr("app_cfg", "label", "ready"));
    TEST_ASSERT_EQUAL_UINT(1, native_nvs::writeCount());

    TEST_ASSERT_TRUE(Esp32BaseConfig::setStrDeferred("app_cfg", "label", "ready", 1000));

    TEST_ASSERT_EQUAL_UINT8(0, Esp32BaseConfig::pendingCount());
    TEST_ASSERT_TRUE(Esp32BaseConfig::flushAll());
    TEST_ASSERT_EQUAL_UINT(1, native_nvs::writeCount());
}

void test_deferred_int_same_pending_value_does_not_extend_due_time() {
    resetConfigHarness();

    TEST_ASSERT_TRUE(Esp32BaseConfig::setIntDeferred("app_cfg", "counter", 7, 100));
    g_nativeMillis = 50;
    TEST_ASSERT_TRUE(Esp32BaseConfig::setIntDeferred("app_cfg", "counter", 7, 1000));

    g_nativeMillis = 101;
    TEST_ASSERT_TRUE(Esp32BaseConfig::flushNextDue());
    TEST_ASSERT_EQUAL_UINT8(0, Esp32BaseConfig::pendingCount());
    TEST_ASSERT_EQUAL_INT32(7, Esp32BaseConfig::getInt("app_cfg", "counter", 0));
}

void test_deferred_bool_same_pending_value_does_not_extend_due_time() {
    resetConfigHarness();

    TEST_ASSERT_TRUE(Esp32BaseConfig::setBoolDeferred("app_cfg", "enabled", false, 100));
    g_nativeMillis = 50;
    TEST_ASSERT_TRUE(Esp32BaseConfig::setBoolDeferred("app_cfg", "enabled", false, 1000));

    g_nativeMillis = 101;
    TEST_ASSERT_TRUE(Esp32BaseConfig::flushNextDue());
    TEST_ASSERT_EQUAL_UINT8(0, Esp32BaseConfig::pendingCount());
    TEST_ASSERT_FALSE(Esp32BaseConfig::getBool("app_cfg", "enabled", true));
}

void test_deferred_string_same_pending_value_does_not_extend_due_time() {
    resetConfigHarness();

    TEST_ASSERT_TRUE(Esp32BaseConfig::setStrDeferred("app_cfg", "label", "ready", 100));
    g_nativeMillis = 50;
    TEST_ASSERT_TRUE(Esp32BaseConfig::setStrDeferred("app_cfg", "label", "ready", 1000));

    g_nativeMillis = 101;
    TEST_ASSERT_TRUE(Esp32BaseConfig::flushNextDue());
    TEST_ASSERT_EQUAL_UINT8(0, Esp32BaseConfig::pendingCount());
    char label[16];
    TEST_ASSERT_TRUE(Esp32BaseConfig::getStr("app_cfg", "label", label, sizeof(label), ""));
    TEST_ASSERT_EQUAL_STRING("ready", label);
}

void test_internal_uint32_state_distinguishes_missing_and_preserves_all_bits() {
    resetConfigHarness();
    uint32_t value = 123;
    TEST_ASSERT_EQUAL(esp32base_internal::ConfigUInt32ReadResult::NotFound,
                      esp32base_internal::readConfigUInt32("eb_app_events", "active_id_bits", value));
    TEST_ASSERT_EQUAL_UINT32(0, value);
    TEST_ASSERT_TRUE(esp32base_internal::writeConfigUInt32(
        "eb_app_events", "active_id_bits", 0x80000001UL));
    TEST_ASSERT_EQUAL_UINT(1, native_nvs::writeCount());
    TEST_ASSERT_TRUE(esp32base_internal::writeConfigUInt32(
        "eb_app_events", "active_id_bits", 0x80000001UL));
    TEST_ASSERT_EQUAL_UINT(1, native_nvs::writeCount());
    TEST_ASSERT_EQUAL(esp32base_internal::ConfigUInt32ReadResult::Found,
                      esp32base_internal::readConfigUInt32("eb_app_events", "active_id_bits", value));
    TEST_ASSERT_EQUAL_HEX32(0x80000001UL, value);
}

void test_internal_uint32_state_rejects_wrong_nvs_type() {
    resetConfigHarness();
    TEST_ASSERT_TRUE(Esp32BaseConfig::setInt("eb_app_events", "active_id_bits", 1));
    uint32_t value = 0;
    TEST_ASSERT_EQUAL(esp32base_internal::ConfigUInt32ReadResult::Error,
                      esp32base_internal::readConfigUInt32("eb_app_events", "active_id_bits", value));
}

void test_internal_uint32_write_repairs_wrong_nvs_type() {
    resetConfigHarness();
    TEST_ASSERT_TRUE(Esp32BaseConfig::setInt("eb_app_events", "active_id_bits", 1));
    const unsigned writesBefore = native_nvs::writeCount();
    TEST_ASSERT_TRUE(esp32base_internal::writeConfigUInt32(
        "eb_app_events", "active_id_bits", 1));
    TEST_ASSERT_EQUAL_UINT(writesBefore + 1U, native_nvs::writeCount());
    uint32_t value = 0;
    TEST_ASSERT_EQUAL(esp32base_internal::ConfigUInt32ReadResult::Found,
                      esp32base_internal::readConfigUInt32("eb_app_events", "active_id_bits", value));
    TEST_ASSERT_EQUAL_UINT32(1, value);
}

void test_internal_remove_config_key_preserves_sibling_keys() {
    resetConfigHarness();
    TEST_ASSERT_TRUE(Esp32BaseConfig::setStr("app_cfg", "registered", "custom"));
    TEST_ASSERT_TRUE(Esp32BaseConfig::setInt("app_cfg", "private", 42));

    TEST_ASSERT_EQUAL(esp32base_internal::ConfigKeyRemoveResult::Removed,
                      esp32base_internal::removeConfigKey("app_cfg", "registered"));
    char value[16] = "";
    TEST_ASSERT_FALSE(Esp32BaseConfig::getStr("app_cfg", "registered", value, sizeof(value), "default"));
    TEST_ASSERT_EQUAL_STRING("default", value);
    TEST_ASSERT_EQUAL_INT32(42, Esp32BaseConfig::getInt("app_cfg", "private", 0));
    TEST_ASSERT_EQUAL(esp32base_internal::ConfigKeyRemoveResult::NotFound,
                      esp32base_internal::removeConfigKey("app_cfg", "registered"));
}

void test_internal_remove_config_key_cancels_pending_write() {
    resetConfigHarness();
    TEST_ASSERT_TRUE(Esp32BaseConfig::setStrDeferred("app_pending", "value", "custom", 1000));
    TEST_ASSERT_EQUAL_UINT8(1, Esp32BaseConfig::pendingCount());

    TEST_ASSERT_EQUAL(esp32base_internal::ConfigKeyRemoveResult::Removed,
                      esp32base_internal::removeConfigKey("app_pending", "value"));
    TEST_ASSERT_EQUAL_UINT8(0, Esp32BaseConfig::pendingCount());
    TEST_ASSERT_TRUE(Esp32BaseConfig::flushAll());
    char value[16] = "";
    TEST_ASSERT_FALSE(Esp32BaseConfig::getStr("app_pending", "value", value, sizeof(value), "default"));
    TEST_ASSERT_EQUAL_STRING("default", value);
}

void test_internal_remove_config_key_reports_lookup_failure_without_clearing_pending() {
    resetConfigHarness();
    TEST_ASSERT_TRUE(Esp32BaseConfig::setIntDeferred("app_fail", "value", 7, 1000));
    native_nvs::store()["app_fail"];
    native_nvs::openFailureNamespace() = "app_fail";

    TEST_ASSERT_EQUAL(esp32base_internal::ConfigKeyRemoveResult::Error,
                      esp32base_internal::removeConfigKey("app_fail", "value"));
    TEST_ASSERT_EQUAL_UINT8(1, Esp32BaseConfig::pendingCount());
    native_nvs::openFailureNamespace().clear();
    TEST_ASSERT_EQUAL(esp32base_internal::ConfigKeyRemoveResult::Removed,
                      esp32base_internal::removeConfigKey("app_fail", "value"));
    TEST_ASSERT_EQUAL_UINT8(0, Esp32BaseConfig::pendingCount());
}

void test_factory_reset_clears_app_event_condition_state() {
    resetConfigHarness();
    const uint8_t recoveryConfig[] = {1, 1, 0, 0x10, 0x27, 0, 0};
    TEST_ASSERT_TRUE(Esp32BaseConfig::setBlob(
        "eb_wifi_rcv", "button", recoveryConfig, sizeof(recoveryConfig)));
    TEST_ASSERT_TRUE(esp32base_internal::writeConfigUInt32(
        "eb_app_events", "active_id_bits", 1));
    TEST_ASSERT_TRUE(Esp32BaseConfig::factoryReset());
    uint8_t recovered[sizeof(recoveryConfig)] = {};
    TEST_ASSERT_FALSE(Esp32BaseConfig::getBlob(
        "eb_wifi_rcv", "button", recovered, sizeof(recovered)));
    uint32_t value = 123;
    TEST_ASSERT_EQUAL(esp32base_internal::ConfigUInt32ReadResult::NotFound,
                      esp32base_internal::readConfigUInt32("eb_app_events", "active_id_bits", value));
}

void test_factory_reset_reports_condition_namespace_open_failure() {
    resetConfigHarness();
    TEST_ASSERT_TRUE(esp32base_internal::writeConfigUInt32(
        "eb_app_events", "active_id_bits", 1));
    native_nvs::openFailureNamespace() = "eb_app_events";
    TEST_ASSERT_FALSE(Esp32BaseConfig::factoryReset());
    native_nvs::openFailureNamespace().clear();
    uint32_t value = 0;
    TEST_ASSERT_EQUAL(esp32base_internal::ConfigUInt32ReadResult::Found,
                      esp32base_internal::readConfigUInt32("eb_app_events", "active_id_bits", value));
    TEST_ASSERT_EQUAL_UINT32(1, value);
}

void test_factory_reset_reports_wifi_recovery_namespace_open_failure() {
    resetConfigHarness();
    const uint8_t recoveryConfig[] = {1, 1, 0, 0x10, 0x27, 0, 0};
    TEST_ASSERT_TRUE(Esp32BaseConfig::setBlob(
        "eb_wifi_rcv", "button", recoveryConfig, sizeof(recoveryConfig)));
    native_nvs::openFailureNamespace() = "eb_wifi_rcv";
    TEST_ASSERT_FALSE(Esp32BaseConfig::factoryReset());
    native_nvs::openFailureNamespace().clear();
    uint8_t recovered[sizeof(recoveryConfig)] = {};
    TEST_ASSERT_TRUE(Esp32BaseConfig::getBlob(
        "eb_wifi_rcv", "button", recovered, sizeof(recovered)));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(recoveryConfig, recovered, sizeof(recoveryConfig));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_uptime_formatter_supports_more_than_32_bit_millis);
    RUN_TEST(test_recovery_button_triggers_at_threshold_once_per_press);
    RUN_TEST(test_recovery_button_release_and_bounce_restart_hold_window);
    RUN_TEST(test_deferred_int_skips_when_nvs_already_has_same_value);
    RUN_TEST(test_deferred_bool_skips_when_nvs_already_has_same_value);
    RUN_TEST(test_deferred_string_skips_when_nvs_already_has_same_value);
    RUN_TEST(test_deferred_int_same_pending_value_does_not_extend_due_time);
    RUN_TEST(test_deferred_bool_same_pending_value_does_not_extend_due_time);
    RUN_TEST(test_deferred_string_same_pending_value_does_not_extend_due_time);
    RUN_TEST(test_internal_uint32_state_distinguishes_missing_and_preserves_all_bits);
    RUN_TEST(test_internal_uint32_state_rejects_wrong_nvs_type);
    RUN_TEST(test_internal_uint32_write_repairs_wrong_nvs_type);
    RUN_TEST(test_internal_remove_config_key_preserves_sibling_keys);
    RUN_TEST(test_internal_remove_config_key_cancels_pending_write);
    RUN_TEST(test_internal_remove_config_key_reports_lookup_failure_without_clearing_pending);
    RUN_TEST(test_factory_reset_clears_app_event_condition_state);
    RUN_TEST(test_factory_reset_reports_condition_namespace_open_failure);
    RUN_TEST(test_factory_reset_reports_wifi_recovery_namespace_open_failure);
    return UNITY_END();
}
