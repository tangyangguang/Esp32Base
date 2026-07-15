#include <unity.h>

#include "core/Esp32BaseConfig.h"
#include "core/internal/Esp32BaseConfigInternal.h"
#include "Preferences.h"

uint32_t g_nativeMillis = 0;
NativeSerial Serial;

static void resetConfigHarness() {
    native_nvs::reset();
    g_nativeMillis = 0;
    Esp32BaseConfig::resumeDeferredFlush();
    Esp32BaseConfig::enableConfigAudit(false);
    Esp32BaseConfig::enableConfigReadAudit(false);
    Esp32BaseConfig::clearNamespace("app_cfg");
    TEST_ASSERT_EQUAL_UINT8(0, Esp32BaseConfig::pendingCount());
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

void test_factory_reset_clears_app_event_condition_state() {
    resetConfigHarness();
    TEST_ASSERT_TRUE(esp32base_internal::writeConfigUInt32(
        "eb_app_events", "active_id_bits", 1));
    TEST_ASSERT_TRUE(Esp32BaseConfig::factoryReset());
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

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_deferred_int_skips_when_nvs_already_has_same_value);
    RUN_TEST(test_deferred_bool_skips_when_nvs_already_has_same_value);
    RUN_TEST(test_deferred_string_skips_when_nvs_already_has_same_value);
    RUN_TEST(test_deferred_int_same_pending_value_does_not_extend_due_time);
    RUN_TEST(test_deferred_bool_same_pending_value_does_not_extend_due_time);
    RUN_TEST(test_deferred_string_same_pending_value_does_not_extend_due_time);
    RUN_TEST(test_internal_uint32_state_distinguishes_missing_and_preserves_all_bits);
    RUN_TEST(test_internal_uint32_state_rejects_wrong_nvs_type);
    RUN_TEST(test_internal_uint32_write_repairs_wrong_nvs_type);
    RUN_TEST(test_factory_reset_clears_app_event_condition_state);
    RUN_TEST(test_factory_reset_reports_condition_namespace_open_failure);
    return UNITY_END();
}
