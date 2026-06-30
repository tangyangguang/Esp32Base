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
    RUN_TEST(test_native_static_asset_serves_cached_content);
    RUN_TEST(test_native_static_asset_respects_auth_when_required);
    RUN_TEST(test_native_static_asset_rejects_invalid_registration);
    RUN_TEST(test_native_after_format_callback_reports_tools_success_details);
    RUN_TEST(test_native_ota_preflight_accepts_size_within_target_partition);
    RUN_TEST(test_native_ota_preflight_rejects_oversize_before_upload_body);
    RUN_TEST(test_native_ota_preflight_rejects_missing_or_invalid_declared_size);
    return UNITY_END();
}
