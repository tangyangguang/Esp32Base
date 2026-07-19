#include <unity.h>

#include "web/Esp32BaseWeb.h"
#include "web/internal/WebOtaPreflight.h"

static void jsonHandler() {
    char value[16];
    char body[32];
    TEST_ASSERT_TRUE(Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_POST));
    TEST_ASSERT_TRUE(Esp32BaseWeb::hasParam("mode"));
    TEST_ASSERT_TRUE(Esp32BaseWeb::getParam("mode", value, sizeof(value)));
    TEST_ASSERT_EQUAL_STRING("fast", value);
    TEST_ASSERT_TRUE(Esp32BaseWeb::getRequestBody(body, sizeof(body)));
    TEST_ASSERT_EQUAL_STRING("{\"x\":1}", body);
    TEST_ASSERT_TRUE(Esp32BaseWeb::checkPostAllowed("json_handler"));
    Esp32BaseWeb::sendJson(202, "{\"ok\":true}");
}

static void chunkHandler() {
    TEST_ASSERT_TRUE(Esp32BaseWeb::sendResponseHeader("X-Test", "yes"));
    TEST_ASSERT_TRUE(Esp32BaseWeb::beginResponse(201, "text/plain; charset=utf-8", "report.txt"));
    Esp32BaseWeb::sendChunk("hello ");
    const uint8_t bytes[] = {'w', 'e', 'b'};
    Esp32BaseWeb::sendBytes(bytes, sizeof(bytes));
    Esp32BaseWeb::endResponse();
}

static void postGuardHandler() {
    if (!Esp32BaseWeb::checkPostAllowed("guard")) {
        return;
    }
    Esp32BaseWeb::sendText(200, "allowed");
}

static void redirectHandler() {
    Esp32BaseWeb::redirectSeeOther("/done");
}

static void routedHandler() {
    char action[16];
    TEST_ASSERT_TRUE(Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_POST));
    TEST_ASSERT_TRUE(Esp32BaseWeb::getParam("action", action, sizeof(action)));
    TEST_ASSERT_EQUAL_STRING("save", action);
    Esp32BaseWeb::sendText(200, "routed");
}

static void irrigationHandler() {
    Esp32BaseWeb::sendHtml(200, "<h1>Irrigation</h1>");
}

static void combinedRootHandler() {
    Esp32BaseWeb::sendHtml(200, "<h1>Combined home</h1>");
}

static void assertRedirect(const char* path, const char* expectedLocation) {
    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch(path, Esp32BaseWeb::METHOD_GET));
    TEST_ASSERT_EQUAL(302, Esp32BaseWeb::nativeTestResponse().code);
    TEST_ASSERT_EQUAL_STRING(expectedLocation, Esp32BaseWeb::nativeTestResponseHeader("Location"));
}

struct AfterFormatState {
    uint8_t calls;
    Esp32BaseWeb::FormatFsResult last;
    void* userSeen;
};

static void afterFormatCallback(const Esp32BaseWeb::FormatFsResult& result, void* user) {
    AfterFormatState* state = static_cast<AfterFormatState*>(user);
    TEST_ASSERT_NOT_NULL(state);
    state->calls++;
    state->last = result;
    state->userSeen = user;
}

void test_native_static_asset_serves_cached_content() {
    Esp32BaseWeb::nativeTestReset();
    static const uint8_t asset[] = "body{color:#123}";

    TEST_ASSERT_TRUE(Esp32BaseWeb::addStaticAsset("/assets/app.css",
                                                  "text/css; charset=utf-8",
                                                  asset,
                                                  sizeof(asset) - 1,
                                                  3600,
                                                  true));

    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/assets/app.css", Esp32BaseWeb::METHOD_GET));

    const Esp32BaseWeb::NativeTestResponse& response = Esp32BaseWeb::nativeTestResponse();
    TEST_ASSERT_EQUAL(200, response.code);
    TEST_ASSERT_EQUAL_STRING("text/css; charset=utf-8", response.contentType.c_str());
    TEST_ASSERT_EQUAL_STRING("body{color:#123}", response.body.c_str());
    TEST_ASSERT_EQUAL_STRING("private, max-age=3600", Esp32BaseWeb::nativeTestResponseHeader("Cache-Control"));
    TEST_ASSERT_EQUAL_STRING("nosniff", Esp32BaseWeb::nativeTestResponseHeader("X-Content-Type-Options"));
}

void test_native_static_asset_respects_auth_when_required() {
    Esp32BaseWeb::nativeTestReset();
    static const uint8_t asset[] = "secret";

    TEST_ASSERT_TRUE(Esp32BaseWeb::addStaticAsset("/assets/secret.js",
                                                  "application/javascript; charset=utf-8",
                                                  asset,
                                                  sizeof(asset) - 1,
                                                  0,
                                                  true));
    Esp32BaseWeb::nativeTestBeginRequest(Esp32BaseWeb::METHOD_GET, "/assets/secret.js");
    Esp32BaseWeb::nativeTestSetAuthenticated(false);

    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/assets/secret.js", Esp32BaseWeb::METHOD_GET));

    TEST_ASSERT_EQUAL(401, Esp32BaseWeb::nativeTestResponse().code);
}

void test_native_static_asset_rejects_invalid_registration() {
    Esp32BaseWeb::nativeTestReset();
    static const uint8_t asset[] = "x";

    TEST_ASSERT_FALSE(Esp32BaseWeb::addStaticAsset("assets/app.css", "text/css", asset, 1));
    TEST_ASSERT_FALSE(Esp32BaseWeb::addStaticAsset("/assets/app.css", "", asset, 1));
    TEST_ASSERT_FALSE(Esp32BaseWeb::addStaticAsset("/assets/app.css", "text/css", nullptr, 1));
    TEST_ASSERT_FALSE(Esp32BaseWeb::addStaticAsset("/assets/app.css", "text/css", asset, 0));
}

void test_native_request_inputs_and_json_response() {
    Esp32BaseWeb::nativeTestReset();
    Esp32BaseWeb::nativeTestBeginRequest(Esp32BaseWeb::METHOD_POST, "/api/run");
    Esp32BaseWeb::nativeTestSetParam("mode", "fast");
    Esp32BaseWeb::nativeTestSetBody("{\"x\":1}");
    Esp32BaseWeb::nativeTestSetAuthenticated(true);
    Esp32BaseWeb::nativeTestSetSameOrigin(true);

    Esp32BaseWeb::nativeTestRun(jsonHandler);

    const Esp32BaseWeb::NativeTestResponse& response = Esp32BaseWeb::nativeTestResponse();
    TEST_ASSERT_EQUAL(202, response.code);
    TEST_ASSERT_EQUAL_STRING("application/json", response.contentType.c_str());
    TEST_ASSERT_EQUAL_STRING("{\"ok\":true}", response.body.c_str());
}

void test_native_request_accessors_report_truncation() {
    Esp32BaseWeb::nativeTestReset();
    Esp32BaseWeb::nativeTestBeginRequest(Esp32BaseWeb::METHOD_POST, "/api/run");
    Esp32BaseWeb::nativeTestSetParam("mode", "long-value");
    Esp32BaseWeb::nativeTestSetBody("{\"long\":true}");

    char value[5];
    char body[8];
    TEST_ASSERT_FALSE(Esp32BaseWeb::getParam("mode", value, sizeof(value)));
    TEST_ASSERT_EQUAL_STRING("long", value);
    TEST_ASSERT_FALSE(Esp32BaseWeb::getRequestBody(body, sizeof(body)));
    TEST_ASSERT_EQUAL_STRING("{\"long\"", body);
}

void test_native_response_stream_and_headers_are_captured() {
    Esp32BaseWeb::nativeTestReset();
    Esp32BaseWeb::nativeTestBeginRequest(Esp32BaseWeb::METHOD_GET, "/download");

    Esp32BaseWeb::nativeTestRun(chunkHandler);

    const Esp32BaseWeb::NativeTestResponse& response = Esp32BaseWeb::nativeTestResponse();
    TEST_ASSERT_EQUAL(201, response.code);
    TEST_ASSERT_EQUAL_STRING("text/plain; charset=utf-8", response.contentType.c_str());
    TEST_ASSERT_EQUAL_STRING("hello web", response.body.c_str());
    TEST_ASSERT_EQUAL_STRING("yes", Esp32BaseWeb::nativeTestResponseHeader("X-Test"));
    TEST_ASSERT_EQUAL_STRING("attachment; filename=\"report.txt\"",
                             Esp32BaseWeb::nativeTestResponseHeader("Content-Disposition"));
}

void test_native_post_guard_captures_method_auth_and_origin_failures() {
    Esp32BaseWeb::nativeTestReset();

    Esp32BaseWeb::nativeTestBeginRequest(Esp32BaseWeb::METHOD_GET, "/guard");
    Esp32BaseWeb::nativeTestSetAuthenticated(true);
    Esp32BaseWeb::nativeTestSetSameOrigin(true);
    Esp32BaseWeb::nativeTestRun(postGuardHandler);
    TEST_ASSERT_EQUAL(405, Esp32BaseWeb::nativeTestResponse().code);
    TEST_ASSERT_EQUAL_STRING("Method Not Allowed", Esp32BaseWeb::nativeTestResponse().body.c_str());

    Esp32BaseWeb::nativeTestBeginRequest(Esp32BaseWeb::METHOD_POST, "/guard");
    Esp32BaseWeb::nativeTestSetAuthenticated(false);
    Esp32BaseWeb::nativeTestSetSameOrigin(true);
    Esp32BaseWeb::nativeTestRun(postGuardHandler);
    TEST_ASSERT_EQUAL(401, Esp32BaseWeb::nativeTestResponse().code);

    Esp32BaseWeb::nativeTestBeginRequest(Esp32BaseWeb::METHOD_POST, "/guard");
    Esp32BaseWeb::nativeTestSetAuthenticated(true);
    Esp32BaseWeb::nativeTestSetSameOrigin(false);
    Esp32BaseWeb::nativeTestRun(postGuardHandler);
    TEST_ASSERT_EQUAL(403, Esp32BaseWeb::nativeTestResponse().code);
    TEST_ASSERT_EQUAL_STRING("Forbidden", Esp32BaseWeb::nativeTestResponse().body.c_str());
}

void test_native_redirect_see_other_is_captured() {
    Esp32BaseWeb::nativeTestReset();
    Esp32BaseWeb::nativeTestBeginRequest(Esp32BaseWeb::METHOD_POST, "/submit");

    Esp32BaseWeb::nativeTestRun(redirectHandler);

    const Esp32BaseWeb::NativeTestResponse& response = Esp32BaseWeb::nativeTestResponse();
    TEST_ASSERT_EQUAL(303, response.code);
    TEST_ASSERT_EQUAL_STRING("text/plain", response.contentType.c_str());
    TEST_ASSERT_EQUAL_STRING("/done", Esp32BaseWeb::nativeTestResponseHeader("Location"));
    TEST_ASSERT_EQUAL_STRING("no-store", Esp32BaseWeb::nativeTestResponseHeader("Cache-Control"));
}

void test_native_dispatch_runs_registered_route_without_losing_request_inputs() {
    Esp32BaseWeb::nativeTestReset();
    TEST_ASSERT_TRUE(Esp32BaseWeb::addApi("/api/routed", routedHandler));
    Esp32BaseWeb::nativeTestBeginRequest(Esp32BaseWeb::METHOD_POST, "/api/routed");
    Esp32BaseWeb::nativeTestSetParam("action", "save");

    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/api/routed", Esp32BaseWeb::METHOD_POST));

    TEST_ASSERT_EQUAL(200, Esp32BaseWeb::nativeTestResponse().code);
    TEST_ASSERT_EQUAL_STRING("routed", Esp32BaseWeb::nativeTestResponse().body.c_str());
}

void test_builtin_routes_home_esp32base_use_canonical_status() {
    Esp32BaseWeb::nativeTestReset();
    Esp32BaseWeb::setHomeMode(Esp32BaseWeb::HOME_ESP32BASE);

    assertRedirect("/", "/esp32base/status");
    assertRedirect("/esp32base", "/esp32base/status");
    TEST_ASSERT_EQUAL_STRING("no-store", Esp32BaseWeb::nativeTestResponseHeader("Cache-Control"));
    assertRedirect("/esp32base/", "/esp32base/status");

    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/esp32base/status", Esp32BaseWeb::METHOD_GET));
    TEST_ASSERT_EQUAL(200, Esp32BaseWeb::nativeTestResponse().code);
    TEST_ASSERT_NOT_EQUAL(-1, static_cast<int>(Esp32BaseWeb::nativeTestResponse().body.find("<h1>Status</h1>")));
}

void test_builtin_routes_home_app_keep_business_root_and_system_status() {
    Esp32BaseWeb::nativeTestReset();
    Esp32BaseWeb::setHomeMode(Esp32BaseWeb::HOME_APP);
    TEST_ASSERT_TRUE(Esp32BaseWeb::setHomePath("/irrigation"));
    TEST_ASSERT_TRUE(Esp32BaseWeb::addPage("/irrigation", "Irrigation", irrigationHandler));

    assertRedirect("/", "/irrigation");
    assertRedirect("/esp32base", "/esp32base/status");
    assertRedirect("/esp32base/", "/esp32base/status");
    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/esp32base/status", Esp32BaseWeb::METHOD_GET));
    TEST_ASSERT_EQUAL(200, Esp32BaseWeb::nativeTestResponse().code);
    TEST_ASSERT_EQUAL(-1, static_cast<int>(Esp32BaseWeb::nativeTestResponse().body.find("/irrigation")));
    TEST_ASSERT_NOT_EQUAL(-1, static_cast<int>(Esp32BaseWeb::nativeTestResponse().body.find("href='/esp32base/status'")));
}

void test_builtin_routes_home_combined_preserve_business_home_behavior() {
    Esp32BaseWeb::nativeTestReset();
    Esp32BaseWeb::setHomeMode(Esp32BaseWeb::HOME_COMBINED);
    TEST_ASSERT_TRUE(Esp32BaseWeb::setHomePath("/irrigation"));
    TEST_ASSERT_TRUE(Esp32BaseWeb::addPage("/irrigation", "Irrigation", irrigationHandler));

    assertRedirect("/", "/irrigation");
    assertRedirect("/esp32base", "/esp32base/status");
    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/esp32base/status", Esp32BaseWeb::METHOD_GET));
    TEST_ASSERT_EQUAL(200, Esp32BaseWeb::nativeTestResponse().code);

    Esp32BaseWeb::nativeTestReset();
    Esp32BaseWeb::setHomeMode(Esp32BaseWeb::HOME_COMBINED);
    TEST_ASSERT_TRUE(Esp32BaseWeb::setHomePath("/"));
    TEST_ASSERT_TRUE(Esp32BaseWeb::addRoute("/", Esp32BaseWeb::METHOD_GET, combinedRootHandler));
    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/", Esp32BaseWeb::METHOD_GET));
    TEST_ASSERT_EQUAL(200, Esp32BaseWeb::nativeTestResponse().code);
    TEST_ASSERT_EQUAL_STRING("<h1>Combined home</h1>", Esp32BaseWeb::nativeTestResponse().body.c_str());
}

void test_status_redirect_preserves_and_encodes_query_parameters() {
    Esp32BaseWeb::nativeTestReset();
    Esp32BaseWeb::nativeTestBeginRequest(Esp32BaseWeb::METHOD_GET, "/esp32base");
    Esp32BaseWeb::nativeTestSetParam("details", "1");
    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/esp32base", Esp32BaseWeb::METHOD_GET));
    TEST_ASSERT_EQUAL_STRING("/esp32base/status?details=1", Esp32BaseWeb::nativeTestResponseHeader("Location"));

    Esp32BaseWeb::nativeTestBeginRequest(Esp32BaseWeb::METHOD_GET, "/esp32base");
    Esp32BaseWeb::nativeTestSetParam("details", "1");
    Esp32BaseWeb::nativeTestSetParam("label", "pump room/1");
    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/esp32base", Esp32BaseWeb::METHOD_GET));
    TEST_ASSERT_EQUAL_STRING("/esp32base/status?details=1&label=pump%20room%2F1",
                             Esp32BaseWeb::nativeTestResponseHeader("Location"));
}

void test_status_auth_footer_recovery_and_system_routes() {
    Esp32BaseWeb::nativeTestReset();
    Esp32BaseWeb::nativeTestBeginRequest(Esp32BaseWeb::METHOD_GET, "/esp32base/status");
    Esp32BaseWeb::nativeTestSetAuthenticated(false);
    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/esp32base/status", Esp32BaseWeb::METHOD_GET));
    TEST_ASSERT_EQUAL(401, Esp32BaseWeb::nativeTestResponse().code);

    Esp32BaseWeb::nativeTestReset();
    Esp32BaseWeb::setFooterBarMode(Esp32BaseWeb::FOOTER_BAR_OFF);
    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/esp32base/status", Esp32BaseWeb::METHOD_GET));
    TEST_ASSERT_EQUAL(-1, static_cast<int>(Esp32BaseWeb::nativeTestResponse().body.find("<footer")));
    TEST_ASSERT_NOT_EQUAL(-1, static_cast<int>(Esp32BaseWeb::nativeTestResponse().body.find("href='/esp32base/system'")));

    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/esp32base/system", Esp32BaseWeb::METHOD_GET));
    TEST_ASSERT_EQUAL(200, Esp32BaseWeb::nativeTestResponse().code);
    static const char* actions[] = {
        "hostname", "wifi-recovery", "filelog", "footer-bar", "reboot", "watchdog-trip-reset",
        "format-fs", "logs-clear", "business-records-clear", "app-events-clear"
    };
    for (const char* action : actions) {
        std::string expected = "action='/esp32base/system/";
        expected += action;
        expected += "'";
        TEST_ASSERT_NOT_EQUAL(-1, static_cast<int>(Esp32BaseWeb::nativeTestResponse().body.find(expected)));
    }
    TEST_ASSERT_FALSE(Esp32BaseWeb::nativeTestDispatch("/esp32base/tools", Esp32BaseWeb::METHOD_GET));
    TEST_ASSERT_EQUAL(404, Esp32BaseWeb::nativeTestResponse().code);
    TEST_ASSERT_FALSE(Esp32BaseWeb::nativeTestDispatch("/esp32base/tools/reboot", Esp32BaseWeb::METHOD_POST));
    TEST_ASSERT_EQUAL(404, Esp32BaseWeb::nativeTestResponse().code);
}

void test_other_builtin_page_routes_remain_available() {
    Esp32BaseWeb::nativeTestReset();
    static const char* paths[] = {
        "/esp32base/logs", "/esp32base/app-events", "/esp32base/app-config",
        "/esp32base/wifi", "/esp32base/auth", "/esp32base/ota", "/esp32base/fs"
    };
    for (const char* path : paths) {
        TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch(path, Esp32BaseWeb::METHOD_GET));
        TEST_ASSERT_EQUAL(200, Esp32BaseWeb::nativeTestResponse().code);
    }
}

void test_native_after_format_callback_reports_tools_success_details() {
    Esp32BaseWeb::nativeTestReset();
    AfterFormatState state = {};

    Esp32BaseWeb::setAfterFormatFsCallback(afterFormatCallback, &state);
    Esp32BaseWeb::nativeTestNotifyToolsFormatFsSuccess(true, false);

    TEST_ASSERT_EQUAL_UINT8(1, state.calls);
    TEST_ASSERT_EQUAL_PTR(&state, state.userSeen);
    TEST_ASSERT_EQUAL_STRING("tools", state.last.source);
    TEST_ASSERT_TRUE(state.last.formatSuccess);
    TEST_ASSERT_TRUE(state.last.mountSuccess);
    TEST_ASSERT_FALSE(state.last.fileLogReloadSuccess);
    TEST_ASSERT_EQUAL_UINT8(0, state.last.businessRecordStoreCount);
    TEST_ASSERT_EQUAL_UINT8(0, state.last.businessRecordStoreReloadedCount);
    TEST_ASSERT_TRUE(state.last.businessRecordStoresReloadSuccess);

    Esp32BaseWeb::clearAfterFormatFsCallback();
    Esp32BaseWeb::nativeTestNotifyToolsFormatFsSuccess(true, true);

    TEST_ASSERT_EQUAL_UINT8(1, state.calls);
}

void test_native_ota_preflight_accepts_size_within_target_partition() {
    char error[96];
    size_t declared = 0;

    TEST_ASSERT_TRUE(esp32base_web::parseOtaDeclaredSize("1048576", declared, error, sizeof(error)));
    TEST_ASSERT_TRUE(esp32base_web::validateOtaDeclaredSize(declared, 0x140000, error, sizeof(error)));
    TEST_ASSERT_EQUAL_UINT32(1048576, declared);
    TEST_ASSERT_EQUAL_STRING("", error);
}

void test_native_ota_preflight_rejects_oversize_before_upload_body() {
    char error[96];
    size_t declared = 0;

    TEST_ASSERT_TRUE(esp32base_web::parseOtaDeclaredSize("1327104", declared, error, sizeof(error)));
    TEST_ASSERT_FALSE(esp32base_web::validateOtaDeclaredSize(declared, 0x140000, error, sizeof(error)));
    TEST_ASSERT_EQUAL_STRING("ota size too large: firmware=1.27 MB partition=1.25 MB", error);
}

void test_native_ota_preflight_rejects_missing_or_invalid_declared_size() {
    char error[96];
    size_t declared = 123;

    TEST_ASSERT_FALSE(esp32base_web::parseOtaDeclaredSize("", declared, error, sizeof(error)));
    TEST_ASSERT_EQUAL_UINT32(0, declared);
    TEST_ASSERT_EQUAL_STRING("missing firmware size", error);

    TEST_ASSERT_FALSE(esp32base_web::parseOtaDeclaredSize("12x", declared, error, sizeof(error)));
    TEST_ASSERT_EQUAL_STRING("invalid firmware size", error);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_native_request_inputs_and_json_response);
    RUN_TEST(test_native_request_accessors_report_truncation);
    RUN_TEST(test_native_response_stream_and_headers_are_captured);
    RUN_TEST(test_native_post_guard_captures_method_auth_and_origin_failures);
    RUN_TEST(test_native_redirect_see_other_is_captured);
    RUN_TEST(test_native_dispatch_runs_registered_route_without_losing_request_inputs);
    RUN_TEST(test_builtin_routes_home_esp32base_use_canonical_status);
    RUN_TEST(test_builtin_routes_home_app_keep_business_root_and_system_status);
    RUN_TEST(test_builtin_routes_home_combined_preserve_business_home_behavior);
    RUN_TEST(test_status_redirect_preserves_and_encodes_query_parameters);
    RUN_TEST(test_status_auth_footer_recovery_and_system_routes);
    RUN_TEST(test_other_builtin_page_routes_remain_available);
    RUN_TEST(test_native_static_asset_serves_cached_content);
    RUN_TEST(test_native_static_asset_respects_auth_when_required);
    RUN_TEST(test_native_static_asset_rejects_invalid_registration);
    RUN_TEST(test_native_after_format_callback_reports_tools_success_details);
    RUN_TEST(test_native_ota_preflight_accepts_size_within_target_partition);
    RUN_TEST(test_native_ota_preflight_rejects_oversize_before_upload_body);
    RUN_TEST(test_native_ota_preflight_rejects_missing_or_invalid_declared_size);
    return UNITY_END();
}
