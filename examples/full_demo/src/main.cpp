#include <Arduino.h>
#include <Esp32Base.h>
#include <WiFi.h>

static const char* APP_NS = "demo";
static const char* APP_KEY_BOOT = "boot";
static const char* APP_KEY_VALUE = "value";

#ifndef ESP32BASE_FULL_DEMO_SELFTEST
#define ESP32BASE_FULL_DEMO_SELFTEST 0
#endif

#if ESP32BASE_FULL_DEMO_SELFTEST
static bool g_selfTestDone = false;

bool selfTestRequest(const char* method, const char* path, const char* body, bool auth, int expectedCode, const char* mustContain) {
    WiFiClient client;
    const IPAddress targetIp = Esp32BaseWiFi::state() == Esp32BaseWiFi::CONFIG_PORTAL ? WiFi.softAPIP() : WiFi.localIP();
    if (!client.connect(targetIp, 80)) {
        ESP32BASE_LOG_E("selftest", "%s %s connect_failed", method, path);
        return false;
    }
    client.print(method);
    client.print(" ");
    client.print(path);
    client.print(" HTTP/1.1\r\nHost: ");
    client.print(targetIp);
    client.print("\r\nConnection: close\r\n");
    if (auth) {
        client.print("Authorization: Basic YWRtaW46YWRtaW4=\r\n");
    }
    const size_t bodyLen = body ? strlen(body) : 0;
    if (bodyLen > 0) {
        client.print("Content-Type: application/x-www-form-urlencoded\r\nContent-Length: ");
        client.print(bodyLen);
        client.print("\r\n");
    }
    client.print("\r\n");
    if (bodyLen > 0) {
        client.print(body);
    }

    char response[900];
    size_t used = 0;
    const uint32_t deadline = millis() + 5000UL;
    while (millis() < deadline && (client.connected() || client.available())) {
        while (client.available()) {
            const char c = static_cast<char>(client.read());
            if (used + 1 < sizeof(response)) {
                response[used++] = c;
            }
        }
        Esp32Base::handle();
        Esp32BaseWatchdog::feed();
        delay(1);
    }
    response[used] = '\0';
    client.stop();

    int code = 0;
    sscanf(response, "HTTP/%*s %d", &code);
    const bool contains = !mustContain || strstr(response, mustContain);
    const bool ok = code == expectedCode && contains;
    ESP32BASE_LOG_I("selftest", "%s %s code=%d expected=%d contains=%s result=%s",
                    method, path, code, expectedCode, mustContain ? mustContain : "-",
                    ok ? "pass" : "fail");
    return ok;
}

void runSelfTest() {
    static uint32_t lastWaitLogMs = 0;
    if (g_selfTestDone) {
        return;
    }
    if (!Esp32BaseWeb::isReady()) {
        const uint32_t now = millis();
        if (now - lastWaitLogMs >= 5000UL) {
            lastWaitLogMs = now;
            ESP32BASE_LOG_I("selftest", "waiting wifi=%s web=%s state=%s",
                            Esp32BaseWiFi::isConnected() ? "yes" : "no",
                            Esp32BaseWeb::isReady() ? "yes" : "no",
                            Esp32BaseWiFi::stateName());
        }
        return;
    }
    g_selfTestDone = true;
    const IPAddress targetIp = Esp32BaseWiFi::state() == Esp32BaseWiFi::CONFIG_PORTAL ? WiFi.softAPIP() : WiFi.localIP();
    ESP32BASE_LOG_I("selftest", "start ip=%s state=%s", targetIp.toString().c_str(), Esp32BaseWiFi::stateName());
    const bool wdtRemoved = Esp32BaseWatchdog::removeCurrentTaskForLongOperation();
    uint8_t pass = 0;
    uint8_t total = 0;
#define RUN_SELFTEST(method, path, body, auth, code, contains) do { ++total; if (selfTestRequest(method, path, body, auth, code, contains)) ++pass; } while (0)
    RUN_SELFTEST("GET", "/esp32base/api/status", nullptr, false, 401, "Unauthorized");
    RUN_SELFTEST("GET", "/esp32base/api/status", nullptr, true, 200, "\"profile\":\"FULL\"");
    RUN_SELFTEST("GET", "/esp32base/api/chip", nullptr, true, 200, "\"flash\"");
    RUN_SELFTEST("GET", "/esp32base/api/firmware", nullptr, true, 200, "\"full-demo\"");
    RUN_SELFTEST("GET", "/esp32base/api/ota", nullptr, true, 200, "\"progress\"");
    RUN_SELFTEST("GET", "/esp32base", nullptr, true, 200, "<title>Home</title>");
    RUN_SELFTEST("GET", "/esp32base/wifi", nullptr, true, 200, "<title>WiFi</title>");
    RUN_SELFTEST("GET", "/esp32base/logs", nullptr, true, 200, "File log: <b>enabled</b>");
    RUN_SELFTEST("GET", "/esp32base/ota", nullptr, true, 200, "<title>OTA</title>");
    RUN_SELFTEST("GET", "/esp32base/reboot", nullptr, true, 200, "<title>Reboot</title>");
    RUN_SELFTEST("GET", "/dashboard", nullptr, true, 200, "<title>Dashboard</title>");
    RUN_SELFTEST("GET", "/control", nullptr, true, 200, "<title>Control</title>");
    RUN_SELFTEST("POST", "/api/control", "value=selftest", true, 303, "Location: /control?saved=1");
    RUN_SELFTEST("GET", "/dashboard", nullptr, true, 200, "Stored value: selftest");
    RUN_SELFTEST("POST", "/esp32base/logs/clear", nullptr, true, 303, "Location: /esp32base/logs?cleared=1");
#undef RUN_SELFTEST
    if (wdtRemoved) {
        Esp32BaseWatchdog::restoreCurrentTaskAfterLongOperation();
    }
    ESP32BASE_LOG_I("selftest", "summary pass=%u total=%u", static_cast<unsigned>(pass), static_cast<unsigned>(total));
}
#endif

void handleDashboard() {
    if (!Esp32BaseWeb::checkAuth()) {
        return;
    }
    char value[64] = "";
    Esp32BaseConfig::getStr(APP_NS, APP_KEY_VALUE, value, sizeof(value), "(empty)");
    Esp32BaseWeb::sendHeader("Dashboard");
    Esp32BaseWeb::sendChunk("<h2>Dashboard</h2><p>Firmware: ");
    Esp32BaseWeb::writeHtmlEscaped(Esp32Base::firmwareName());
    Esp32BaseWeb::sendChunk(" ");
    Esp32BaseWeb::writeHtmlEscaped(Esp32Base::firmwareVersion());
    Esp32BaseWeb::sendChunk("</p><p>Stored value: ");
    Esp32BaseWeb::writeHtmlEscaped(value);
    Esp32BaseWeb::sendChunk("</p><p><a href='/esp32base/logs'>Logs</a> <a href='/esp32base/ota'>OTA</a></p>");
    Esp32BaseWeb::sendFooter();
}

void handleControl() {
    if (!Esp32BaseWeb::checkAuth()) {
        return;
    }
    Esp32BaseWeb::sendHeader("Control");
    Esp32BaseWeb::sendChunk("<h2>Control</h2><form method='post' action='/api/control' onsubmit='return once(this)'>Value<input name='value' maxlength='48'><input type='submit' value='Save'></form>");
#if ESP32BASE_ENABLE_SLEEP
    Esp32BaseWeb::sendChunk("<form method='post' action='/api/control' onsubmit=\"return confirm('Enter deep sleep?')&&once(this)\"><input type='hidden' name='sleep' value='1'><input type='submit' value='Deep Sleep 10s' style='background:#c33'></form>");
#endif
    Esp32BaseWeb::sendFooter();
}

void handleControlApi() {
    if (!Esp32BaseWeb::checkAuth()) {
        return;
    }
    if (Esp32BaseWeb::hasParam("sleep")) {
#if ESP32BASE_ENABLE_SLEEP
        Esp32BaseWeb::sendText(200, "sleeping");
        delay(200);
        Esp32BaseSleep::deepSleepSeconds(10);
#else
        Esp32BaseWeb::sendText(409, "sleep disabled");
#endif
        return;
    }
    char value[64] = "";
    if (Esp32BaseWeb::getParam("value", value, sizeof(value))) {
        Esp32BaseConfig::setStr(APP_NS, APP_KEY_VALUE, value);
    }
    Esp32BaseWeb::redirectSeeOther("/control?saved=1");
}

void setup() {
    Esp32Base::setFirmwareInfo("full-demo", "0.1.0");
    Esp32Base::setHostname("esp32base-full");
    Esp32BaseWeb::setAuth("admin", "admin");
    Esp32BaseConfig::enableConfigAudit(true);
    Esp32Base::begin();
#if ESP32BASE_ENABLE_FILELOG
    Esp32BaseFileLog::enable("/logs/eb_app.log", 32UL * 1024UL, Esp32BaseLog::INFO, 4);
#endif
    const int32_t boot = Esp32BaseConfig::getInt(APP_NS, APP_KEY_BOOT, 0) + 1;
    Esp32BaseConfig::setIntDeferred(APP_NS, APP_KEY_BOOT, boot);
    Esp32BaseWeb::addPage("/dashboard", handleDashboard);
    Esp32BaseWeb::addPage("/control", handleControl);
    Esp32BaseWeb::addApi("/api/control", handleControlApi);
    ESP32BASE_LOG_I("example", "full demo ready boot=%ld", static_cast<long>(boot));
}

void loop() {
    Esp32Base::handle();
#if ESP32BASE_FULL_DEMO_SELFTEST
    runSelfTest();
#endif
    delay(10);
}
