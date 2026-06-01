#include <unity.h>

#include "web/Esp32BaseWeb.h"

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

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_native_request_inputs_and_json_response);
    RUN_TEST(test_native_response_stream_and_headers_are_captured);
    RUN_TEST(test_native_post_guard_captures_method_auth_and_origin_failures);
    RUN_TEST(test_native_redirect_see_other_is_captured);
    RUN_TEST(test_native_dispatch_runs_registered_route_without_losing_request_inputs);
    return UNITY_END();
}
