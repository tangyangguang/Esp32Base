#include <Arduino.h>
#include <Esp32Base.h>
#include <WiFi.h>

static const char* APP_NS = "demo";
static const char* APP_KEY_BOOT = "boot";
static const char* APP_KEY_META = "meta";
static const char* APP_KEY_VALUE = "value";
static const char* APP_KEY_CODE = "code";
static const char* APP_KEY_LIMIT = "limit";
static const char* APP_KEY_MULTIPLIER = "mult";
static const char* APP_KEY_OFFSET = "offset";
static const char* APP_KEY_TIMEOUT = "timeout";
static const char* APP_KEY_ENABLED = "enabled";
static const char* APP_KEY_SAFE = "safe";
static const char* APP_KEY_MODE = "mode";
static const char* APP_KEY_PROFILE = "profile";
static const char* APP_KEY_MIN_RAW = "minraw";

struct DemoMeta {
    uint32_t boot;
    uint32_t nextId;
    uint32_t marker;
};

const Esp32BaseAppConfig::EnumOption MODE_OPTIONS[] = {
    {"auto", "Auto"},
    {"manual", "Manual"},
    {"service", "Service"}
};

const Esp32BaseAppConfig::EnumOption PROFILE_OPTIONS[] = {
    {"quiet", "Quiet"},
    {"balanced", "Balanced"},
    {"fast", "Fast"}
};

bool validateAsciiValue(const Esp32BaseAppConfig::FieldRef&, const Esp32BaseAppConfig::FieldValue& value, char* error, size_t errorLen) {
    const char* s = value.text ? value.text : "";
    for (size_t i = 0; s[i]; ++i) {
        const char c = s[i];
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
        if (!ok) {
            strlcpy(error, "Value may contain only letters, digits, underscore and hyphen.", errorLen);
            return false;
        }
    }
    return true;
}

bool validateAppConfigPage(char* error, size_t errorLen) {
    int32_t limit = 0;
    int32_t multiplier = 0;
    if (!Esp32BaseAppConfig::submittedInt(APP_NS, APP_KEY_LIMIT, limit) ||
        !Esp32BaseAppConfig::submittedDecimal(APP_NS, APP_KEY_MULTIPLIER, multiplier)) {
        strlcpy(error, "Submitted App Config values are unavailable.", errorLen);
        return false;
    }
    if (multiplier > limit * 100) {
        strlcpy(error, "Multiplier cannot be greater than limit.", errorLen);
        return false;
    }
    return true;
}

void onAppConfigChange(const Esp32BaseAppConfig::Change& change) {
    ESP32BASE_LOG_I("appcfg", "changed key=%s old=%s new=%s",
                    change.field.key,
                    change.oldValue.text ? change.oldValue.text : "-",
                    change.newValue.text ? change.newValue.text : "-");
}

void onAppConfigSave(const Esp32BaseAppConfig::SaveSummary& summary) {
    ESP32BASE_LOG_I("appcfg", "save changed=%u saved=%u failed=%u restart=%s",
                    summary.changedCount,
                    summary.savedCount,
                    summary.failedCount,
                    summary.restartRequired ? "yes" : "no");
}

#ifndef ESP32BASE_FULL_DEMO_SELFTEST
#define ESP32BASE_FULL_DEMO_SELFTEST 0
#endif

#if ESP32BASE_FULL_DEMO_SELFTEST
static bool g_selfTestDone = false;

struct SelfTestRequestJob {
    const char* method;
    const char* path;
    const char* body;
    bool auth;
    const char* mustContain;
    IPAddress targetIp;
    volatile bool done;
    bool connected;
    bool contains;
    int code;
    char statusLine[96];
};

size_t selfTestAdvanceMatch(const char* pattern, size_t patternLen, size_t matched, char c) {
    while (matched > 0 && c != pattern[matched]) {
        size_t next = matched - 1;
        while (next > 0 && strncmp(pattern, pattern + matched - next, next) != 0) {
            --next;
        }
        matched = next;
    }
    if (c == pattern[matched]) {
        ++matched;
        if (matched > patternLen) {
            matched = patternLen;
        }
    }
    return matched;
}

void selfTestRequestTask(void* arg) {
    SelfTestRequestJob* job = static_cast<SelfTestRequestJob*>(arg);
    WiFiClient client;
    client.setTimeout(1000);
    job->connected = client.connect(job->targetIp, 80);
    if (!job->connected) {
        job->done = true;
        vTaskDelete(nullptr);
        return;
    }

    client.print(job->method);
    client.print(" ");
    client.print(job->path);
    client.print(" HTTP/1.1\r\nHost: ");
    client.print(job->targetIp);
    client.print("\r\nConnection: close\r\n");
    if (job->auth) {
        client.print("Authorization: Basic YWRtaW46YWRtaW4=\r\n");
    }
    const size_t bodyLen = job->body ? strlen(job->body) : 0;
    if (bodyLen > 0) {
        client.print("Content-Type: application/x-www-form-urlencoded\r\nContent-Length: ");
        client.print(bodyLen);
        client.print("\r\n");
    }
    client.print("\r\n");
    if (bodyLen > 0) {
        client.print(job->body);
    }

    size_t statusUsed = 0;
    size_t matchUsed = 0;
    const size_t matchLen = job->mustContain ? strlen(job->mustContain) : 0;
    job->contains = !job->mustContain || matchLen == 0;
    bool firstLineDone = false;
    const uint32_t startMs = millis();
    while ((millis() - startMs) < 15000UL && (client.connected() || client.available())) {
        while (client.available()) {
            const char c = static_cast<char>(client.read());
            if (!firstLineDone) {
                if (c == '\n') {
                    firstLineDone = true;
                } else if (c != '\r' && statusUsed + 1 < sizeof(job->statusLine)) {
                    job->statusLine[statusUsed++] = c;
                }
            }
            if (!job->contains && job->mustContain) {
                matchUsed = selfTestAdvanceMatch(job->mustContain, matchLen, matchUsed, c);
                if (matchUsed == matchLen) {
                    job->contains = true;
                }
            }
        }
        delay(1);
    }
    job->statusLine[statusUsed] = '\0';
    client.stop();

    sscanf(job->statusLine, "HTTP/%*s %d", &job->code);
    job->done = true;
    vTaskDelete(nullptr);
}

bool selfTestRequest(const char* method, const char* path, const char* body, bool auth, int expectedCode, const char* mustContain) {
    SelfTestRequestJob job = {
        method,
        path,
        body,
        auth,
        mustContain,
        Esp32BaseWiFi::state() == Esp32BaseWiFi::CONFIG_PORTAL ? WiFi.softAPIP() : WiFi.localIP(),
        false,
        false,
        false,
        0,
        ""
    };

    TaskHandle_t task = nullptr;
    if (xTaskCreate(selfTestRequestTask, "eb_selftest_http", 4096, &job, 1, &task) != pdPASS) {
        ESP32BASE_LOG_E("selftest", "%s %s task_create_failed", method, path);
        return false;
    }
    while (!job.done) {
        Esp32Base::handle();
        Esp32BaseWatchdog::feed();
        delay(1);
    }

    if (!job.connected) {
        ESP32BASE_LOG_E("selftest", "%s %s connect_failed", method, path);
        return false;
    }

    const bool ok = job.code == expectedCode && job.contains;
    ESP32BASE_LOG_I("selftest", "%s %s code=%d expected=%d contains=%s result=%s",
                    method, path, job.code, expectedCode, mustContain ? mustContain : "-",
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
#define RUN_BOOL(expr) do { ++total; if (expr) ++pass; } while (0)
    RUN_SELFTEST("GET", "/esp32base/api/status", nullptr, false, 401, "Unauthorized");
    RUN_SELFTEST("GET", "/esp32base/api/status", nullptr, true, 200, "\"profile\":\"FULL\"");
    RUN_SELFTEST("GET", "/esp32base/api/chip", nullptr, true, 200, "\"flash\"");
    RUN_SELFTEST("GET", "/esp32base/api/chip", nullptr, true, 200, "\"chipModel\"");
    RUN_SELFTEST("GET", "/esp32base/api/chip", nullptr, true, 200, "\"efuseMac\"");
    RUN_SELFTEST("GET", "/esp32base/api/firmware", nullptr, true, 200, "\"full-demo\"");
    RUN_SELFTEST("GET", "/esp32base/api/ota", nullptr, true, 200, "\"progress\"");
    RUN_SELFTEST("GET", "/", nullptr, true, 302, "Location: /dashboard");
    RUN_SELFTEST("GET", "/esp32base", nullptr, true, 200, "<title>Status</title>");
    RUN_SELFTEST("GET", "/esp32base", nullptr, true, 200, "panel statuspage");
    RUN_SELFTEST("GET", "/esp32base", nullptr, true, 200, "<div class='tablewrap'><table class='kv'>");
    RUN_SELFTEST("GET", "/esp32base", nullptr, true, 200, "<h2>Device</h2>");
    RUN_SELFTEST("GET", "/esp32base", nullptr, true, 200, "Name</th>");
    RUN_SELFTEST("GET", "/esp32base", nullptr, true, 200, "Hostname</th>");
    RUN_SELFTEST("GET", "/esp32base", nullptr, true, 200, "full-demo 1.0.0");
    RUN_SELFTEST("GET", "/esp32base", nullptr, true, 200, "Profile</th>");
    RUN_SELFTEST("GET", "/esp32base", nullptr, true, 200, "Uptime</th>");
    RUN_SELFTEST("GET", "/esp32base", nullptr, true, 200, "Boot count</th>");
    RUN_SELFTEST("GET", "/esp32base", nullptr, true, 200, "Firmware &amp; OTA");
    RUN_SELFTEST("GET", "/esp32base", nullptr, true, 200, "Runtime Health");
    RUN_SELFTEST("GET", "/esp32base", nullptr, true, 200, "Network");
    RUN_SELFTEST("GET", "/esp32base", nullptr, true, 200, "Storage &amp; Logs");
    RUN_SELFTEST("GET", "/esp32base", nullptr, true, 200, "Hardware");
    RUN_SELFTEST("GET", "/esp32base", nullptr, true, 200, "Partition Table");
    RUN_SELFTEST("GET", "/esp32base", nullptr, true, 200, "IP</th>");
    RUN_SELFTEST("GET", "/esp32base", nullptr, true, 200, "RSSI</th>");
    RUN_SELFTEST("GET", "/esp32base", nullptr, true, 200, "STA MAC</th>");
    RUN_SELFTEST("GET", "/esp32base", nullptr, true, 200, "AP MAC</th>");
    RUN_SELFTEST("GET", "/esp32base", nullptr, true, 200, "Last reset</th>");
    RUN_SELFTEST("GET", "/esp32base", nullptr, true, 200, "Last wake</th>");
    RUN_SELFTEST("GET", "/esp32base", nullptr, true, 200, "Chip</th>");
    RUN_SELFTEST("GET", "/esp32base", nullptr, true, 200, "eFuse MAC</th>");
    RUN_SELFTEST("GET", "/esp32base", nullptr, true, 200, "class='submetrics'");
    RUN_SELFTEST("GET", "/esp32base", nullptr, true, 200, "Max alloc</b>");
    RUN_SELFTEST("GET", "/esp32base", nullptr, true, 200, "Watchdog</th>");
    RUN_SELFTEST("GET", "/esp32base", nullptr, true, 200, "Lifetime resets</b>");
    RUN_SELFTEST("GET", "/esp32base", nullptr, true, 200, "Trip resets</b>");
    RUN_SELFTEST("GET", "/esp32base", nullptr, true, 200, "Trip reset at</b>");
    RUN_SELFTEST("GET", "/esp32base", nullptr, true, 200, "<header class='pagehead'><h1>Full Demo</h1></header><div class='statusgrid'><section class='panel statuspage'><h2>Device</h2>");
    RUN_SELFTEST("GET", "/esp32base", nullptr, true, 200, "<div class='statusgrid'>");
    RUN_SELFTEST("GET", "/esp32base", nullptr, true, 200, "Log level</b>");
    RUN_SELFTEST("GET", "/esp32base", nullptr, true, 200, "Current file</b>");
    RUN_SELFTEST("GET", "/esp32base", nullptr, true, 200, "Limit</b>");
    RUN_SELFTEST("GET", "/esp32base", nullptr, true, 200, "File inventory</th>");
    RUN_SELFTEST("GET", "/esp32base", nullptr, true, 200, "href='/esp32base/fs'");
    RUN_SELFTEST("GET", "/esp32base", nullptr, true, 200, "Current firmware</th>");
    RUN_SELFTEST("GET", "/esp32base", nullptr, true, 200, "OTA headroom</th>");
    RUN_SELFTEST("GET", "/esp32base", nullptr, true, 200, "target slot - current sketch");
    RUN_SELFTEST("GET", "/esp32base", nullptr, true, 200, "Rollback</th>");
    RUN_SELFTEST("GET", "/esp32base", nullptr, true, 200, "WiFi</th>");
    RUN_SELFTEST("GET", "/esp32base", nullptr, true, 200, "File log</th>");
    RUN_SELFTEST("GET", "/esp32base", nullptr, true, 200, "app0");
    RUN_SELFTEST("GET", "/esp32base", nullptr, true, 200, "app1");
    RUN_SELFTEST("GET", "/esp32base", nullptr, true, 200, "nvs");
    RUN_SELFTEST("GET", "/esp32base", nullptr, true, 200, "spiffs");
    RUN_SELFTEST("GET", "/esp32base", nullptr, true, 200, "next OTA");
    RUN_SELFTEST("GET", "/esp32base/wifi", nullptr, true, 200, "<title>Network</title>");
    RUN_SELFTEST("GET", "/esp32base/wifi", nullptr, true, 200, "<section class='panel formpanel wifipanel'><h2>Credentials</h2><form class='editform'");
    RUN_SELFTEST("GET", "/esp32base/wifi", nullptr, true, 200, "<section class='panel dangerpanel'><h2>Clear WiFi</h2>");
    RUN_SELFTEST("GET", "/esp32base/wifi", nullptr, true, 200, "Password (optional)");
    RUN_SELFTEST("GET", "/esp32base/wifi", nullptr, true, 200, "class='secondary' type='button' value='Show/Hide Password'");
    RUN_SELFTEST("GET", "/esp32base/wifi?saved=1", nullptr, true, 200, "Credentials updated and connection started.");
    RUN_SELFTEST("GET", "/esp32base/wifi?error=clear_failed", nullptr, true, 200, "WiFi credentials were not cleared");
    RUN_SELFTEST("GET", "/esp32base/logs", nullptr, true, 200, "<section class='panel logpanel'><div class='tablewrap'><table class='logmeta'>");
    RUN_SELFTEST("GET", "/esp32base/logs", nullptr, true, 200, "<th>File log</th><td><span class='tag ok'>enabled</span>");
    RUN_SELFTEST("GET", "/esp32base/logs", nullptr, true, 200, "class='logmeta'");
    RUN_SELFTEST("GET", "/esp32base/logs", nullptr, true, 200, "class='segname'");
    RUN_SELFTEST("GET", "/esp32base/logs", nullptr, true, 200, "class='segsize'");
    RUN_SELFTEST("GET", "/esp32base/logs", nullptr, true, 200, "<th>Max per file</th><td>32.00 KB");
    RUN_SELFTEST("GET", "/esp32base/logs", nullptr, true, 200, "Open raw log</a>");
    RUN_SELFTEST("GET", "/esp32base/logs", nullptr, true, 200, "class='active' href='/esp32base/logs?segment=0'><span class='segname'>current-0");
    RUN_SELFTEST("GET", "/esp32base/logs?segment=1", nullptr, true, 200, "history-1");
    RUN_SELFTEST("GET", "/esp32base/logs?segment=99", nullptr, true, 200, "class='active' href='/esp32base/logs?segment=0'><span class='segname'>current-0");
    RUN_SELFTEST("GET", "/esp32base/logs?cleared=1", nullptr, true, 200, "Logs cleared");
    RUN_SELFTEST("GET", "/esp32base/logs?error=clear_failed", nullptr, true, 200, "Logs action failed");
    RUN_SELFTEST("GET", "/esp32base/fs", nullptr, false, 401, "Unauthorized");
    RUN_SELFTEST("GET", "/esp32base/fs", nullptr, true, 200, "<title>File system</title>");
    RUN_SELFTEST("GET", "/esp32base/fs", nullptr, true, 200, "Summary");
    RUN_SELFTEST("GET", "/esp32base/fs", nullptr, true, 200, "class='fsummary'");
    RUN_SELFTEST("GET", "/esp32base/fs", nullptr, true, 200, "Largest files");
    RUN_SELFTEST("GET", "/esp32base/fs", nullptr, true, 200, "File tree");
    RUN_SELFTEST("GET", "/esp32base/fs", nullptr, true, 200, "Files</b>");
    RUN_SELFTEST("GET", "/esp32base/fs", nullptr, true, 200, "Dirs</b>");
    RUN_SELFTEST("GET", "/esp32base/fs", nullptr, true, 200, "Listed size</b>");
    RUN_SELFTEST("GET", "/esp32base/fs", nullptr, true, 200, "action='/esp32base/fs/download'");
    RUN_SELFTEST("GET", "/esp32base/fs/download?path=%2Fmissing.tmp", nullptr, true, 404, "File not found");
    RUN_SELFTEST("GET", "/esp32base/fs?manage=1", nullptr, true, 200, "File management mode");
    RUN_SELFTEST("GET", "/esp32base/fs?manage=1", nullptr, true, 200, "<th>Action</th>");
    RUN_SELFTEST("POST", "/esp32base/fs/delete", "path=%2Fmissing.tmp", true, 303, "Location: /esp32base/fs?manage=1&error=delete_missing");
    RUN_SELFTEST("GET", "/esp32base/auth", nullptr, true, 200, "<title>Auth</title>");
    RUN_SELFTEST("GET", "/esp32base/auth", nullptr, true, 200, "<section class='panel formpanel authpanel'><h2>Credentials</h2><form class='editform'");
    RUN_SELFTEST("GET", "/esp32base/auth", nullptr, true, 200, "type='password'");
    RUN_SELFTEST("GET", "/esp32base/auth", nullptr, true, 200, "Confirm new auth password");
    RUN_SELFTEST("GET", "/esp32base/auth", nullptr, true, 200, "New passwords do not match");
    RUN_SELFTEST("GET", "/esp32base/ota", nullptr, true, 200, "<title>OTA</title>");
    RUN_SELFTEST("GET", "/esp32base/ota", nullptr, true, 200, "<section class='panel formpanel uploadpanel'><h2>Firmware upload</h2>");
    RUN_SELFTEST("GET", "/esp32base/tools", nullptr, true, 200, "<title>System</title>");
    RUN_SELFTEST("GET", "/esp32base/tools", nullptr, true, 200, "<section class='panel actionpanel'><h2>System settings</h2>");
    RUN_SELFTEST("GET", "/esp32base/tools", nullptr, true, 200, "System settings");
    RUN_SELFTEST("GET", "/esp32base/tools", nullptr, true, 200, "WiFi Setup");
    RUN_SELFTEST("GET", "/esp32base/tools", nullptr, true, 200, "Web Auth");
    RUN_SELFTEST("GET", "/esp32base/tools", nullptr, true, 200, "App Config");
    RUN_SELFTEST("GET", "/esp32base/tools", nullptr, true, 200, "class='toollinks'");
    RUN_SELFTEST("GET", "/esp32base/tools", nullptr, true, 200, "Application configuration values");
    RUN_SELFTEST("GET", "/esp32base/tools", nullptr, true, 200, "Stored credentials used by station mode and WiFi recovery.");
    RUN_SELFTEST("GET", "/esp32base/tools", nullptr, true, 200, "HTTP Basic Auth credentials for built-in routes.");
    RUN_SELFTEST("GET", "/esp32base/tools", nullptr, true, 200, "Authenticated firmware upload endpoint.");
    RUN_SELFTEST("GET", "/esp32base/tools", nullptr, true, 200, "Firmware OTA");
    RUN_SELFTEST("GET", "/esp32base/tools", nullptr, true, 200, "Read-only LittleFS inventory and size summary.");
    RUN_SELFTEST("GET", "/esp32base/tools", nullptr, true, 200, "class='toolgrid'");
    RUN_SELFTEST("GET", "/esp32base/tools", nullptr, true, 200, "<section class='panel formpanel hostpanel'><h2>Hostname</h2>");
    RUN_SELFTEST("GET", "/esp32base/tools", nullptr, true, 200, "class='hostfacts'");
    RUN_SELFTEST("GET", "/esp32base/tools", nullptr, true, 200, "<b>Restart</b>");
    RUN_SELFTEST("GET", "/esp32base/tools", nullptr, true, 200, "class='hostedit'");
    RUN_SELFTEST("GET", "/esp32base/tools", nullptr, true, 200, "Hostname");
    RUN_SELFTEST("GET", "/esp32base/tools", nullptr, true, 200, "Save Hostname");
    RUN_SELFTEST("GET", "/esp32base/tools", nullptr, true, 200, "Watchdog trip");
    RUN_SELFTEST("GET", "/esp32base/tools", nullptr, true, 200, "Reset Watchdog Trip");
    RUN_SELFTEST("GET", "/esp32base/tools", nullptr, true, 200, "action='/esp32base/tools/watchdog-trip-reset'");
    RUN_SELFTEST("GET", "/esp32base/tools", nullptr, true, 200, "Restart device");
    RUN_SELFTEST("GET", "/esp32base/tools", nullptr, true, 200, "<section class='panel dangerpanel'><h2>Restart device</h2>");
    RUN_SELFTEST("GET", "/esp32base/tools", nullptr, true, 200, "Format LittleFS");
    RUN_SELFTEST("GET", "/esp32base/tools", nullptr, true, 200, "Free heap:");
    RUN_SELFTEST("GET", "/esp32base/tools", nullptr, true, 200, "RSSI:");
    RUN_SELFTEST("GET", "/esp32base/tools?hostname_saved=1", nullptr, true, 200, "Hostname saved");
    RUN_SELFTEST("GET", "/esp32base/tools?formatted=1", nullptr, true, 200, "LittleFS formatted");
    RUN_SELFTEST("GET", "/esp32base/app-config", nullptr, true, 200, "<title>App Config</title>");
    RUN_SELFTEST("GET", "/esp32base/app-config", nullptr, true, 200, "Application configuration values stored by Esp32Base.");
    RUN_SELFTEST("GET", "/esp32base/app-config", nullptr, true, 200, "Stored value");
    RUN_SELFTEST("GET", "/esp32base/app-config", nullptr, true, 200, "Device code");
    RUN_SELFTEST("GET", "/esp32base/app-config", nullptr, true, 200, "Confirm changes");
    RUN_SELFTEST("GET", "/esp32base/app-config", nullptr, true, 200, "id='acbox' class='confirmbox'><h2>Confirm changes</h2>");
    RUN_SELFTEST("GET", "/esp32base/app-config", nullptr, true, 200, "Save App Config");
    RUN_SELFTEST("GET", "/esp32base/app-config", nullptr, true, 200, "data-group='General'");
    RUN_SELFTEST("GET", "/esp32base/app-config", nullptr, true, 200, "class=\"acgroup\"");
    RUN_SELFTEST("GET", "/esp32base/app-config", nullptr, true, 200, "class='tag warn restart'>restart</span>");
    RUN_SELFTEST("GET", "/esp32base/app-config", nullptr, true, 200, "Multiplier");
    RUN_SELFTEST("GET", "/esp32base/app-config", nullptr, true, 200, "Offset");
    RUN_SELFTEST("GET", "/esp32base/app-config", nullptr, true, 200, "Timeout");
    RUN_SELFTEST("GET", "/esp32base/app-config", nullptr, true, 200, "Safe mode");
    RUN_SELFTEST("GET", "/esp32base/app-config", nullptr, true, 200, "Mode");
    RUN_SELFTEST("GET", "/esp32base/app-config", nullptr, true, 200, "Profile");
    RUN_SELFTEST("GET", "/esp32base/app-config", nullptr, true, 200, "Raw minimum");
    RUN_SELFTEST("GET", "/esp32base/app-config", nullptr, true, 200, "-2147483648");
    RUN_SELFTEST("POST", "/esp32base/app-config", "rev=1&f0=demo&f1=A1&f2=50&f3=1.25&f4=-0.50&f5=30&f6=1&f8=auto&f9=balanced&f10=-2147483648", true, 303, "Location: /esp32base/app-config");
    RUN_SELFTEST("GET", "/dashboard", nullptr, true, 200, "<title>Dashboard</title>");
    RUN_SELFTEST("GET", "/control", nullptr, true, 200, "<title>Control</title>");
    RUN_SELFTEST("GET", "/control", nullptr, true, 200, "<style id='full-demo-head-extra'>");
    RUN_SELFTEST("GET", "/control", nullptr, true, 200, "href='/control'");
    RUN_SELFTEST("GET", "/control", nullptr, true, 200, "type='checkbox'");
    RUN_SELFTEST("GET", "/control", nullptr, true, 200, "type='radio'");
    RUN_SELFTEST("GET", "/control/edit", nullptr, true, 200, "href='/control'");
    RUN_SELFTEST("GET", "/control/edit", nullptr, true, 200, "<footer class='footerbar'><span class='syslinks'>");
    RUN_SELFTEST("GET", "/ui-status", nullptr, true, 200, "状态概览模板");
    RUN_SELFTEST("GET", "/ui-stats", nullptr, true, 200, "统计摘要模板");
    RUN_SELFTEST("GET", "/ui-records?page=2", nullptr, true, 200, "共 128 条 / 7 页");
    RUN_SELFTEST("GET", "/ui-records?page=2", nullptr, true, 200, "page=1'>上一页");
    RUN_SELFTEST("GET", "/ui-config?saved=1", nullptr, true, 200, "保存成功");
    RUN_SELFTEST("GET", "/ui-action", nullptr, true, 200, "操作命令模板");
    RUN_SELFTEST("POST", "/ui-action/run", "run=1", true, 303, "Location: /ui-action?saved=1");
    RUN_SELFTEST("GET", "/ui-flow?saved=1", nullptr, true, 200, "流程向导模板");
    RUN_SELFTEST("GET", "/ui-maintenance", nullptr, true, 200, "诊断维护模板");
    RUN_SELFTEST("GET", "/ui-access", nullptr, false, 200, "访问控制模板");
    RUN_SELFTEST("GET", "/api/control", nullptr, true, 200, "\"method\":\"GET\"");
    RUN_SELFTEST("GET", "/api/csv", nullptr, true, 200, "Content-Type: text/csv");
    RUN_SELFTEST("GET", "/api/csv", nullptr, true, 200, "Content-Disposition: attachment");
    RUN_SELFTEST("POST", "/api/control", "value=selftest", true, 303, "Location: /control?saved=1");
    RUN_SELFTEST("GET", "/dashboard", nullptr, true, 200, "Stored value: selftest");
    RUN_SELFTEST("POST", "/esp32base/logs/clear", nullptr, true, 303, "Location: /esp32base/logs?cleared=1");
    RUN_SELFTEST("GET", "/esp32base/logs?cleared=1", nullptr, true, 200, "Logs cleared");
    RUN_BOOL(Esp32BaseWeb::verifyAuth("admin", "admin"));
    RUN_BOOL(Esp32BaseWeb::saveAuth("selftest_user", "selftestPass1"));
    RUN_BOOL(Esp32BaseWeb::verifyAuth("selftest_user", "selftestPass1"));
    Esp32BaseWeb::setDefaultAuth("admin", "admin");
    RUN_BOOL(!Esp32BaseWeb::verifyAuth("admin", "admin"));
    RUN_BOOL(Esp32BaseWeb::verifyAuth("selftest_user", "selftestPass1"));
    RUN_BOOL(Esp32BaseWeb::resetAuth());
    RUN_BOOL(Esp32BaseWeb::verifyAuth("admin", "admin"));
#undef RUN_BOOL
#undef RUN_SELFTEST
    if (wdtRemoved) {
        Esp32BaseWatchdog::restoreCurrentTaskAfterLongOperation();
    }
    ESP32BASE_LOG_I("selftest", "summary pass=%u total=%u", static_cast<unsigned>(pass), static_cast<unsigned>(total));
}
#endif

void handleHeadExtra() {
    Esp32BaseWeb::sendChunk("<style id='full-demo-head-extra'>nav a.active{font-weight:700}</style>");
}

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
    Esp32BaseWeb::sendChunk("<h2>Control</h2><form method='post' action='/api/control' onsubmit='return once(this)'>Value<input name='value' maxlength='48'>Limit<input type='number' name='limit' min='0' max='100' value='50'>PIN<input type='password' name='pin' maxlength='8' autocomplete='off'><p><label><input type='checkbox' name='enabled' value='1'> Enabled</label></p><p><label><input type='radio' name='mode' value='auto' checked> Auto</label> <label><input type='radio' name='mode' value='manual'> Manual</label></p><input type='submit' value='Save'></form>");
#if ESP32BASE_ENABLE_SLEEP
    Esp32BaseWeb::sendChunk("<form method='post' action='/api/control' onsubmit=\"return confirm('Enter deep sleep?')&&once(this)\"><input type='hidden' name='sleep' value='1'><input type='submit' value='Deep Sleep 10s' style='background:#c33'></form>");
#endif
    Esp32BaseWeb::sendFooter();
}

void handleControlApi() {
    if (!Esp32BaseWeb::checkAuth()) {
        return;
    }
    if (Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_GET)) {
        char value[64] = "";
        Esp32BaseConfig::getStr(APP_NS, APP_KEY_VALUE, value, sizeof(value), "");
        Esp32BaseWeb::beginJson(200);
        Esp32BaseWeb::sendChunk("\"method\":\"");
        Esp32BaseWeb::writeJsonEscaped(Esp32BaseWeb::currentMethodName());
        Esp32BaseWeb::sendChunk("\",\"value\":\"");
        Esp32BaseWeb::writeJsonEscaped(value);
        Esp32BaseWeb::sendChunk("\"");
        Esp32BaseWeb::endJson();
        return;
    }
    if (!Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_POST)) {
        Esp32BaseWeb::sendText(405, "method not allowed");
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

void handleUiActionRun() {
    if (!Esp32BaseWeb::checkAuth()) {
        return;
    }
    if (!Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_POST)) {
        Esp32BaseWeb::sendText(405, "method not allowed");
        return;
    }
    Esp32BaseWeb::redirectSeeOther("/ui-action?saved=1");
}

void handleCsvApi() {
    if (!Esp32BaseWeb::checkAuth()) {
        return;
    }
    if (!Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_GET)) {
        Esp32BaseWeb::sendText(405, "method not allowed");
        return;
    }
    char value[64] = "";
    Esp32BaseConfig::getStr(APP_NS, APP_KEY_VALUE, value, sizeof(value), "");
    if (!Esp32BaseWeb::beginCsv(200, "full_demo.csv")) {
        return;
    }
    Esp32BaseWeb::sendChunk("key,value\r\n");
    Esp32BaseWeb::sendChunk("method,");
    Esp32BaseWeb::writeCsvEscaped(Esp32BaseWeb::currentMethodName());
    Esp32BaseWeb::sendChunk("\r\nstored,");
    Esp32BaseWeb::writeCsvEscaped(value);
    Esp32BaseWeb::sendChunk("\r\n");
    Esp32BaseWeb::endResponse();
}

const Esp32BaseWeb::ResultNotice DEMO_RESULTS[] = {
    {"saved", "1", Esp32BaseWeb::UI_OK, "保存成功", "页面已通过 POST -> 303 -> GET 返回。"},
    {"error", "invalid", Esp32BaseWeb::UI_WARN, "提交未保存", "示例参数未通过校验。"}
};

void handleUiStatusDemo() {
    if (!Esp32BaseWeb::checkAuth()) {
        return;
    }
    Esp32BaseWeb::sendHeader("UI Status");
    Esp32BaseWeb::sendPageTitle("状态概览模板", "用于设备首页和需要快速判断当前状态的页面。");
    Esp32BaseWeb::sendResultNotice(DEMO_RESULTS, 2);
    Esp32BaseWeb::beginPanel("当前状态");
    Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_OK, "设备正常", "当前空闲，下一计划 18:30。");
    Esp32BaseWeb::beginMetricGrid();
    Esp32BaseWeb::sendMetric("当前状态", "空闲");
    Esp32BaseWeb::sendMetric("下一计划", "18:30");
    Esp32BaseWeb::sendMetric("可用通道", "4 路");
    Esp32BaseWeb::sendMetric("异常", "0");
    Esp32BaseWeb::endMetricGrid();
    Esp32BaseWeb::endPanel();
    Esp32BaseWeb::sendFooter();
}

void handleUiStatsDemo() {
    if (!Esp32BaseWeb::checkAuth()) {
        return;
    }
    Esp32BaseWeb::sendHeader("UI Stats");
    Esp32BaseWeb::sendPageTitle("统计摘要模板", "用轻量数字、表格和 CSS 结构表达汇总，不引入图表库。");
    Esp32BaseWeb::beginPanel("本月摘要");
    Esp32BaseWeb::beginMetricGrid();
    Esp32BaseWeb::sendMetric("本月次数", "128");
    Esp32BaseWeb::sendMetric("本月用量", "32.4L");
    Esp32BaseWeb::sendMetric("成功率", "98%");
    Esp32BaseWeb::sendMetric("较上周", "+6%");
    Esp32BaseWeb::endMetricGrid();
    Esp32BaseWeb::sendInfoRowCompactLink("分组汇总", "用紧凑表格或 CSS 条形表达，不依赖图表库。", "4 组",
                                         "/ui-records", "查看记录");
    Esp32BaseWeb::endPanel();
    Esp32BaseWeb::sendFooter();
}

void handleUiRecordsDemo() {
    if (!Esp32BaseWeb::checkAuth()) {
        return;
    }
    char pageText[12] = "1";
    Esp32BaseWeb::getParam("page", pageText, sizeof(pageText));
    uint32_t page = static_cast<uint32_t>(strtoul(pageText, nullptr, 10));
    if (page == 0) {
        page = 1;
    }
    Esp32BaseWeb::sendHeader("UI Records");
    Esp32BaseWeb::sendPageTitle("列表记录模板", "包含紧凑筛选、表头、分页和空状态。");
    Esp32BaseWeb::beginPanel("最近记录");
    Esp32BaseWeb::sendChunk("<div class='actions'><span class='tag info'>最近 24 小时</span><span class='tag'>全部类型</span><span class='tag'>每页 20 条</span></div>");
    Esp32BaseWeb::sendChunk("<table class='kv'><tr><th>时间</th><th>类型</th><th>结果</th></tr><tr><td>18:30</td><td>计划执行</td><td>完成</td></tr><tr><td>16:10</td><td>手动执行</td><td>完成</td></tr></table>");
    Esp32BaseWeb::Pagination pagination = {"/ui-records", "range=24h&type=all", page, 20, 128};
    Esp32BaseWeb::sendPagination(pagination);
    Esp32BaseWeb::endPanel();
    Esp32BaseWeb::sendFooter();
}

void handleUiConfigDemo() {
    if (!Esp32BaseWeb::checkAuth()) {
        return;
    }
    Esp32BaseWeb::sendHeader("UI Config");
    Esp32BaseWeb::sendPageTitle("配置编辑模板", "简单字段行内改，多字段对象进入独立编辑页。");
    Esp32BaseWeb::sendResultNotice(DEMO_RESULTS, 2);
    Esp32BaseWeb::beginPanel("紧凑配置列表");
    Esp32BaseWeb::sendInfoRowCompactLink("第 1 路名称", "用于页面和记录展示，不影响实际控制。", "花坛",
                                         "/ui-config?saved=1", "展开修改");
    Esp32BaseWeb::sendInfoRowCompactLink("默认浇水计划", "包含时间、通道、执行天数和目标量。", "3 条",
                                         "/ui-flow", "进入编辑页");
    Esp32BaseWeb::sendInfoRowCompactLink("恢复出厂", "高风险操作，必须进入确认保护页。", nullptr,
                                         "/ui-maintenance", "确认页", Esp32BaseWeb::UI_DANGER);
    Esp32BaseWeb::endPanel();
    Esp32BaseWeb::sendFooter();
}

void handleUiActionDemo() {
    if (!Esp32BaseWeb::checkAuth()) {
        return;
    }
    Esp32BaseWeb::sendHeader("UI Action");
    Esp32BaseWeb::sendPageTitle("操作命令模板", "一次性动作与长期配置分离。");
    Esp32BaseWeb::sendResultNotice(DEMO_RESULTS, 2);
    Esp32BaseWeb::beginPanel("立即执行");
    Esp32BaseWeb::sendInfoRowCompactForm("可执行", "设备空闲、时间可信、无严重异常。", nullptr,
                                         "/ui-action/run", "开始", "run", "1");
    Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_WARN, "拒绝执行示例", "当前时间未同步时，应说明原因和下一步。");
    Esp32BaseWeb::endPanel();
    Esp32BaseWeb::sendFooter();
}

void handleUiFlowDemo() {
    if (!Esp32BaseWeb::checkAuth()) {
        return;
    }
    Esp32BaseWeb::sendHeader("UI Flow");
    Esp32BaseWeb::sendPageTitle("流程向导模板", "用于校准、首次设置和复杂维护。");
    Esp32BaseWeb::sendResultNotice(DEMO_RESULTS, 2);
    Esp32BaseWeb::beginPanel("校准流程");
    Esp32BaseWeb::sendInfoRowCompact("1. 依据", "展示旧值、数据来源和是否允许继续。", "通过");
    Esp32BaseWeb::sendInfoRowCompactLink("2. 实测", "录入真实测量结果，不塞进普通配置表。", nullptr,
                                         "/ui-flow", "继续");
    Esp32BaseWeb::sendInfoRowCompactLink("3. 核对保存", "展示旧值、新值、变化幅度和影响范围。", nullptr,
                                         "/ui-flow?saved=1", "保存");
    Esp32BaseWeb::endPanel();
    Esp32BaseWeb::sendFooter();
}

void handleUiMaintenanceDemo() {
    if (!Esp32BaseWeb::checkAuth()) {
        return;
    }
    Esp32BaseWeb::sendHeader("UI Maintenance");
    Esp32BaseWeb::sendPageTitle("诊断维护模板", "只读优先，危险动作进入确认保护页。");
    Esp32BaseWeb::beginPanel("系统诊断");
    Esp32BaseWeb::sendInfoRowCompact("WiFi", "连接状态、RSSI、IP。", "正常");
    Esp32BaseWeb::sendInfoRowCompactLink("维护任务", "导出、扫描、重启等长任务显示状态和下一步。", "空闲",
                                         "/esp32base/tools", "查看");
    Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_WARN, "原始数据受控", "限制长度，可复制或导出，不做无限滚动调试平台。");
    Esp32BaseWeb::endPanel();
    Esp32BaseWeb::sendFooter();
}

void handleUiAccessDemo() {
    Esp32BaseWeb::sendHeader("UI Access");
    Esp32BaseWeb::sendPageTitle("访问控制模板", "覆盖登录、权限不足、会话失效和只读受限。");
    Esp32BaseWeb::beginPanel("受限状态");
    Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_WARN, "需要登录", "登录后可以继续执行配置和维护操作。");
    Esp32BaseWeb::sendInfoRowCompactLink("只读访问", "无权限时展示原因和可继续查看的内容。", "允许",
                                         "/esp32base", "返回状态");
    Esp32BaseWeb::endPanel();
    Esp32BaseWeb::sendFooter();
}

void setup() {
    Esp32Base::setFirmwareInfo("full-demo", "1.0.0");
    Esp32BaseWeb::setDefaultAuth("admin", "admin");
    Esp32BaseWeb::setDeviceName("Full Demo");
    Esp32BaseWeb::setHomePath("/dashboard");
    Esp32BaseWeb::setHomeMode(Esp32BaseWeb::HOME_COMBINED);
    Esp32BaseWeb::setSystemNavMode(Esp32BaseWeb::SYSTEM_NAV_SECTION);
    Esp32BaseWeb::setBuiltinLabel(Esp32BaseWeb::BUILTIN_HOME, "Status");
    Esp32BaseWeb::setBuiltinLabel(Esp32BaseWeb::BUILTIN_WIFI, "Network");
    Esp32BaseWeb::setBuiltinLabel(Esp32BaseWeb::BUILTIN_OTA, "OTA");
    Esp32BaseWeb::setBuiltinLabel(Esp32BaseWeb::BUILTIN_TOOLS, "System");
    Esp32BaseWeb::setHeadExtraCallback(handleHeadExtra);
    Esp32BaseConfig::enableConfigAudit(true);
    Esp32BaseAppConfig::setTitle("App Config");
    Esp32BaseAppConfig::setPageValidateCallback(validateAppConfigPage);
    Esp32BaseAppConfig::setChangeCallback(onAppConfigChange);
    Esp32BaseAppConfig::setSaveCallback(onAppConfigSave);
    Esp32BaseAppConfig::addGroup({"general", "General"});
    Esp32BaseAppConfig::addGroup({"control", "Control"});
    Esp32BaseAppConfig::addGroup({"advanced", "Advanced"});
    Esp32BaseAppConfig::addString({"general", APP_NS, APP_KEY_VALUE, "Stored value", "demo", 1, 48,
                                   "Letters, digits, underscore and hyphen only.", false, validateAsciiValue});
    Esp32BaseAppConfig::addString({"general", APP_NS, APP_KEY_CODE, "Device code", "A1", 2, 8,
                                   "Short ASCII code, 2..8 characters.", true, validateAsciiValue});
    Esp32BaseAppConfig::addInt({"control", APP_NS, APP_KEY_LIMIT, "Limit", 50, 0, 100, 1, nullptr,
                                "Integer range 0..100.", false, nullptr});
    Esp32BaseAppConfig::addDecimal({"control", APP_NS, APP_KEY_MULTIPLIER, "Multiplier", 125, 0, 1000, 1, 2, nullptr,
                                    "Decimal scale 2, step 0.01, stored as int32 raw value.", false, nullptr});
    Esp32BaseAppConfig::addDecimal({"control", APP_NS, APP_KEY_OFFSET, "Offset", -50, -500, 500, 25, 2, "V",
                                    "Signed decimal with 0.25 step.", false, nullptr});
    Esp32BaseAppConfig::addInt({"advanced", APP_NS, APP_KEY_TIMEOUT, "Timeout", 30, 5, 300, 5, "s",
                                "Step-limited integer with seconds unit.", false, nullptr});
    Esp32BaseAppConfig::addBool({"control", APP_NS, APP_KEY_ENABLED, "Enabled", true,
                                 "Boolean field stored in NVS.", false, nullptr});
    Esp32BaseAppConfig::addBool({"advanced", APP_NS, APP_KEY_SAFE, "Safe mode", false,
                                 "Boolean field marked restart-required.", true, nullptr});
    Esp32BaseAppConfig::addEnum({"control", APP_NS, APP_KEY_MODE, "Mode", "auto", MODE_OPTIONS, 3,
                                 "Enum field stored as option value.", true, nullptr});
    Esp32BaseAppConfig::addEnum({"advanced", APP_NS, APP_KEY_PROFILE, "Profile", "balanced", PROFILE_OPTIONS, 3,
                                 "Second enum for option rendering.", false, nullptr});
    Esp32BaseAppConfig::addDecimal({"advanced", APP_NS, APP_KEY_MIN_RAW, "Raw minimum", INT32_MIN, INT32_MIN, INT32_MIN, 1, 0, nullptr,
                                    "Scale 0 INT32_MIN parser boundary.", false, nullptr});
    Esp32Base::begin();
#if ESP32BASE_ENABLE_FILELOG
    if (!Esp32BaseFileLog::isEnabled()) {
#if ESP32BASE_ENABLE_FS
        ESP32BASE_LOG_W("example", "filelog_unavailable fs_ready=%s", Esp32BaseFs::isReady() ? "yes" : "no");
        if (!Esp32BaseFs::isReady() && Esp32BaseFs::format() && Esp32BaseFs::begin()) {
            Esp32BaseFileLog::setMode(Esp32BaseFileLog::INFO);
        }
#endif
    }
#endif
    const int32_t boot = Esp32BaseConfig::getInt(APP_NS, APP_KEY_BOOT, 0) + 1;
    Esp32BaseConfig::setIntDeferred(APP_NS, APP_KEY_BOOT, boot);
    DemoMeta meta = {};
    Esp32BaseConfig::getPod(APP_NS, APP_KEY_META, meta);
    meta.boot = static_cast<uint32_t>(boot);
    meta.nextId += 1U;
    meta.marker = 0xEB32BA5EUL;
    if (Esp32BaseConfig::setPodDeferred(APP_NS, APP_KEY_META, meta)) {
        DemoMeta pendingMeta = {};
        Esp32BaseConfig::getPod(APP_NS, APP_KEY_META, pendingMeta);
        ESP32BASE_LOG_I("example", "demo meta boot=%lu next_id=%lu",
                        static_cast<unsigned long>(pendingMeta.boot),
                        static_cast<unsigned long>(pendingMeta.nextId));
    }
    Esp32BaseWeb::addPage("/dashboard", "Dashboard", handleDashboard);
    Esp32BaseWeb::addPage("/control", "Control", handleControl);
    Esp32BaseWeb::addPage("/ui-status", "UI Status", handleUiStatusDemo);
    Esp32BaseWeb::addPage("/ui-stats", "UI Stats", handleUiStatsDemo);
    Esp32BaseWeb::addPage("/ui-records", "UI Records", handleUiRecordsDemo);
    Esp32BaseWeb::addPage("/ui-config", "UI Config", handleUiConfigDemo);
    Esp32BaseWeb::addPage("/ui-action", "UI Action", handleUiActionDemo);
    Esp32BaseWeb::addPage("/ui-flow", "UI Flow", handleUiFlowDemo);
    Esp32BaseWeb::addPage("/ui-maintenance", "UI Maintenance", handleUiMaintenanceDemo);
    Esp32BaseWeb::addPage("/ui-access", "UI Access", handleUiAccessDemo);
    Esp32BaseWeb::addRoute("/control/edit", Esp32BaseWeb::METHOD_GET, handleControl);
    Esp32BaseWeb::addRoute("/ui-action/run", Esp32BaseWeb::METHOD_POST, handleUiActionRun);
    Esp32BaseWeb::addApi("/api/control", handleControlApi);
    Esp32BaseWeb::addApi("/api/csv", handleCsvApi);
    ESP32BASE_LOG_I("example", "full demo ready boot=%ld", static_cast<long>(boot));
}

void loop() {
    Esp32Base::handle();
#if ESP32BASE_FULL_DEMO_SELFTEST
    runSelfTest();
#endif
    delay(10);
}
