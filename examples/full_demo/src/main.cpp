#include <Arduino.h>
#include <Esp32Base.h>

static const char* APP_NS = "demo";
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

#if defined(ESP32BASE_FULL_DEMO_WIFI_SSID) && defined(ESP32BASE_FULL_DEMO_WIFI_PASS)
#define ESP32BASE_FULL_DEMO_HAS_WIFI_CREDENTIALS 1
#else
#define ESP32BASE_FULL_DEMO_HAS_WIFI_CREDENTIALS 0
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
    Esp32BaseWeb::sendChunk("</p><p><a href='/esp32base/logs'>System Logs</a> <a href='/esp32base/ota'>OTA</a></p>");
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
    if (Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_GET)) {
        if (!Esp32BaseWeb::checkAuth()) {
            return;
        }
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
    if (!Esp32BaseWeb::checkPostAllowed("full_demo_control")) {
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

void setup() {
    Esp32Base::setFirmwareInfo("full-demo", "1.0.0");
    Esp32BaseWeb::setDefaultAuth("admin", "admin");
    Esp32BaseWeb::setDeviceName("Full Demo");
    Esp32BaseWeb::setHomePath("/");
    Esp32BaseWeb::setHomeMode(Esp32BaseWeb::HOME_COMBINED);
    Esp32BaseWeb::setSystemNavMode(Esp32BaseWeb::SYSTEM_NAV_SECTION);
    Esp32BaseWeb::setBuiltinLabel(Esp32BaseWeb::BUILTIN_HOME, "Status");
    Esp32BaseWeb::setBuiltinLabel(Esp32BaseWeb::BUILTIN_WIFI, "Network");
    Esp32BaseWeb::setBuiltinLabel(Esp32BaseWeb::BUILTIN_OTA, "OTA");
    Esp32BaseWeb::setBuiltinLabel(Esp32BaseWeb::BUILTIN_TOOLS, "System");
    Esp32BaseWeb::setHeadExtraCallback(handleHeadExtra);
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
    Esp32Base::begin();
#if ESP32BASE_FULL_DEMO_HAS_WIFI_CREDENTIALS
    Esp32BaseWiFi::connect(ESP32BASE_FULL_DEMO_WIFI_SSID, ESP32BASE_FULL_DEMO_WIFI_PASS, true);
#endif
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
    Esp32BaseWeb::addPage("/", "Dashboard", handleDashboard);
    Esp32BaseWeb::addRoute("/dashboard", Esp32BaseWeb::METHOD_GET, handleDashboard);
    Esp32BaseWeb::addPage("/control", "Control", handleControl);
    Esp32BaseWeb::addApi("/api/control", handleControlApi);
    Esp32BaseWeb::addApi("/api/csv", handleCsvApi);
    ESP32BASE_LOG_I("example", "full demo ready");
}

void loop() {
    Esp32Base::handle();
    delay(10);
}
