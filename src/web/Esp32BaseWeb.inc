#include "../Esp32BaseProfile.h"

#if ESP32BASE_ENABLE_WEB

#include "Esp32BaseWeb.h"
#include "../Esp32Base.h"
#include "../core/Esp32BaseLog.h"
#include "../core/Esp32BaseSystem.h"
#include "../core/Esp32BaseConfig.h"
#include "../network/Esp32BaseWiFi.h"

#if ESP32BASE_ENABLE_FS
#include "../runtime/Esp32BaseFs.h"
#endif
#if ESP32BASE_ENABLE_FILELOG
#include "../runtime/Esp32BaseFileLog.h"
#endif
#if ESP32BASE_ENABLE_NTP
#include "../network/Esp32BaseNtp.h"
#endif
#if ESP32BASE_ENABLE_WATCHDOG
#include "../runtime/Esp32BaseWatchdog.h"
#endif

#if ESP32BASE_ENABLE_BUS
#include "../runtime/Esp32BaseBus.h"
#endif
#if ESP32BASE_ENABLE_OTA
#include "../update/Esp32BaseOta.h"
#endif

#include <WebServer.h>
#include <WiFi.h>
#include <Preferences.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <mbedtls/base64.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

namespace {
struct Route {
    char path[48];
    char title[24];
    Esp32BaseWeb::Method method;
    Esp32BaseWeb::Handler handler;
    bool appPage;
    bool registered;
};

struct NavItem {
    char path[48];
    char title[24];
};

WebServer g_server(80);
Route g_routes[ESP32BASE_WEB_MAX_ROUTES];
NavItem g_navItems[ESP32BASE_WEB_MAX_NAV_ITEMS];
const char* g_headerKeys[] = {"Authorization", "X-Sha256", "X-Firmware-Size", "Host", "Origin", "Referer"};
bool g_webReady = false;
bool g_authEnabled = true;
bool g_defaultAuthSet = false;
bool g_authLoadedFromStorage = false;
char g_defaultAuthUser[32] = "admin";
char g_defaultAuthPass[64] = "admin";
char g_authUser[32] = "admin";
char g_authPass[64] = "admin";
char g_deviceName[32] = "Esp32Base";
char g_homePath[48] = "";
Esp32BaseWeb::HomeMode g_homeMode = Esp32BaseWeb::HOME_ESP32BASE;
Esp32BaseWeb::SystemNavMode g_systemNavMode = Esp32BaseWeb::SYSTEM_NAV_SECTION;
char g_builtinLabels[7][16] = {"Status", "WiFi", "OTA", "Logs", "System", "System", "Auth"};
Esp32BaseWeb::Handler g_headExtraCallback = nullptr;
const char* g_pageHeadProgmem = nullptr;
char g_chunkBuffer[512];
size_t g_chunkUsed = 0;
constexpr size_t kResponseChunkPayloadMax = 128U;
char g_activeUri[48] = "";
Esp32BaseWeb::Method g_currentMethod = Esp32BaseWeb::METHOD_UNKNOWN;
Esp32BaseWeb::Method g_lastRequestMethod = Esp32BaseWeb::METHOD_UNKNOWN;
bool g_requestContextActive = false;
bool g_responseActive = false;
bool g_responseBroken = false;
bool g_authLoggedForRequest = false;
bool g_otaUploadForbidden = false;
bool g_otaUploadStartFailed = false;

#if ESP32BASE_ENABLE_APP_CONFIG
enum AppConfigStoredType : uint8_t {
    APP_CFG_STRING,
    APP_CFG_INT,
    APP_CFG_DECIMAL,
    APP_CFG_BOOL,
    APP_CFG_ENUM
};

struct AppConfigGroupSlot {
    bool used;
    const char* id;
    const char* title;
};

struct AppConfigFieldSlot {
    bool used;
    AppConfigStoredType type;
    const char* groupId;
    const char* ns;
    const char* key;
    const char* label;
    const char* help;
    const char* unit;
    bool restartRequired;
    Esp32BaseAppConfig::FieldValidateCallback validate;
    union {
        Esp32BaseAppConfig::StringField stringField;
        Esp32BaseAppConfig::IntField intField;
        Esp32BaseAppConfig::DecimalField decimalField;
        Esp32BaseAppConfig::BoolField boolField;
        Esp32BaseAppConfig::EnumField enumField;
    } spec;
};

struct AppConfigPendingRestartSlot {
    bool active;
    uint8_t fieldIndex;
    int32_t oldRaw;
    bool oldBool;
    bool oldTextUnavailable;
    char* oldText;
};

AppConfigGroupSlot g_appConfigGroups[ESP32BASE_APP_CONFIG_MAX_GROUPS];
AppConfigFieldSlot g_appConfigFields[ESP32BASE_APP_CONFIG_MAX_FIELDS];
AppConfigPendingRestartSlot g_appConfigPendingRestart[ESP32BASE_APP_CONFIG_MAX_FIELDS];
uint8_t g_appConfigGroupCount = 0;
uint8_t g_appConfigFieldCount = 0;
uint32_t g_appConfigRevision = 1;
char g_appConfigTitle[32] = "App Config";
bool g_appConfigSubmitContext = false;
Esp32BaseAppConfig::PageValidateCallback g_appConfigPageValidate = nullptr;
Esp32BaseAppConfig::ChangeCallback g_appConfigChangeCallback = nullptr;
Esp32BaseAppConfig::SaveCallback g_appConfigSaveCallback = nullptr;
#endif

static const char WEB_HEAD[] PROGMEM =
    "<meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<style>"
    "body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Arial,sans-serif;font-size:14px;line-height:1.45;margin:0;padding:14px;max-width:1040px;margin-inline:auto;color:#1f2933;background:#f6f8f7}"
    "h1,h2,h3{color:#111827;line-height:1.25;margin:0 0 8px}h1{font-size:18px}h2{font-size:16px}h3{font-size:15px}p{margin:0 0 10px}"
    "a{color:#25313f;text-decoration:none}.muted,.info{color:#64707d}.ok{color:#217a4f}.err,.dangertext{color:#b93a32}"
    "nav,.panel,.footerbar{background:#fff;border:1px solid #d8e0e6;border-radius:6px;box-shadow:0 1px 2px rgba(16,24,40,.04)}"
    "nav{margin:0 0 14px;padding:8px 10px}nav a{font-size:14px;font-weight:600;display:inline-block;padding:6px 8px;margin:2px;border-radius:5px;color:#25313f}.brand{font-weight:700}nav a.active{background:#e5f0eb;color:#245f52}"
    ".page{margin:0}.pagehead{background:#fff;border:1px solid #d8e0e6;border-radius:6px;box-shadow:0 1px 2px rgba(16,24,40,.04);padding:10px 12px;margin:0 0 12px}.pagehead h1{margin:0}.panel{padding:12px;margin:12px 0}.panel>:last-child{margin-bottom:0}.actions{display:flex;flex-wrap:wrap;gap:8px;align-items:center;margin-top:10px}"
    "button,input[type=submit],input[type=button]{font-size:14px;background:#24303d;color:#fff;padding:7px 11px;border:0;border-radius:5px;cursor:pointer;margin:0 6px 6px 0}button:hover,input[type=submit]:hover,input[type=button]:hover{background:#111827}.dangerbtn,input.danger{background:#b93a32}.dangerbtn:hover,input.danger:hover{background:#9f2f29}"
    "label{display:block;font-weight:600;margin:0 0 4px}input:not([type]),input[type=text],input[type=password],input[type=number],input[type=email],input[type=url],input[type=tel],input[type=search]{width:100%;font-size:14px;padding:7px 9px;margin:0 0 12px;border:1px solid #ccd5dd;border-radius:5px;box-sizing:border-box;background:#fff;color:#1f2933}"
    "input[type=file]{font-size:14px;margin:2px 0 12px;max-width:100%}progress{height:12px;border:0;border-radius:8px;overflow:hidden;background:#e7ebef}"
    "details{margin:12px 0}summary{cursor:pointer;color:#25313f}.footerbar{margin-top:14px;padding:8px 10px;display:flex;flex-wrap:wrap;align-items:center;justify-content:space-between;gap:10px;color:#64707d;font-size:13px}"
    ".syslinks{min-width:0;flex:1 1 auto;display:flex;flex-wrap:wrap;gap:5px;align-items:center}.syslinks a{font-size:13px;line-height:1.35;padding:4px 7px;margin:0;background:#f1f4f3;color:#64707d;border-radius:5px}.heap{flex:0 0 auto;margin-left:auto;white-space:nowrap;text-align:right;font-size:12px}"
    "@media(max-width:640px){body{padding:10px}nav a{font-size:14px;padding:6px 7px}.footerbar{align-items:flex-start}.heap{margin-left:0;text-align:left;white-space:normal}}"
    ".kv{border-collapse:collapse;width:100%;margin:0}.kv th,.kv td{border-bottom:1px solid #d8e0e6;padding:7px 5px;text-align:left;vertical-align:top}.kv tr:last-child th,.kv tr:last-child td{border-bottom:0}.kv th{width:30%;color:#64707d;font-weight:600}"
    ".part{border-collapse:collapse;width:100%;font-size:13px}.part th,.part td{border-bottom:1px solid #d8e0e6;padding:6px 5px;text-align:left;vertical-align:top}.part th{color:#64707d;font-weight:600}.part tr:last-child td{border-bottom:0}"
    ".tabs,.quicklinks{display:flex;flex-wrap:wrap;gap:6px;margin:8px 0 10px}.tabs a,.quicklinks a{font-size:13px;padding:5px 8px;margin:0;background:#f1f4f3;color:#64707d;border-radius:5px}.tabs a{display:inline-flex;align-items:center;gap:8px}.tabs a.active{background:#e5f0eb;color:#245f52;font-weight:600}.segsize{padding-left:7px;border-left:1px solid #d8e0e6;font-variant-numeric:tabular-nums}"
    ".logmeta{border-collapse:collapse;font-size:13px;line-height:1.45;margin:0}.logmeta th,.logmeta td{padding:2px 10px 2px 0;text-align:left;vertical-align:top}.logmeta th{width:112px;color:#64707d;font-weight:600}.logmeta td{color:#1f2933}.statusline{min-height:1.45em}.toolsection{padding:12px;margin:12px 0;background:#fff;border:1px solid #d8e0e6;border-radius:6px}"
    "pre{white-space:pre;overflow-x:auto;word-break:normal;font-size:12px;line-height:1.45;background:#f1f4f3;border:1px solid #d8e0e6;border-radius:5px;padding:9px}.logframe{display:block;width:100%;height:62vh;min-height:320px;border:1px solid #d8e0e6;border-radius:5px;background:#f1f4f3}"
    "</style>"
    "<script>function once(f){if(f.dataset.busy)return false;f.dataset.busy=1;var b=f.querySelector('[type=submit]');if(b)b.disabled=true;return true;}</script>";

#if ESP32BASE_ENABLE_APP_CONFIG
// Page-scoped CSS for /esp32base/app-config. Kept out of WEB_HEAD so Status, Logs, WiFi,
// Auth, OTA and Tools pages don't pay ~700 B per request for App Config-only styles.
static const char WEB_APPCFG_STYLE[] PROGMEM =
    "<style>"
    ".appcfg{max-width:720px;margin:0 auto}.appcfg .pagehead,.appcfg .panel{margin:10px 0}.appcfg .panel{padding:12px 14px}.appcfg .acfield{margin:0 0 11px}.appcfg .acfield:last-child{margin-bottom:0}.appcfg .acfield>label{margin:0 0 3px}.appcfg input:not([type]),.appcfg input[type=text],.appcfg input[type=number],.appcfg select{font-size:14px;padding:6px 8px;margin:0;border:1px solid #ccd5dd;border-radius:5px;box-sizing:border-box;background:#fff;color:#1f2933}.appcfg input:not([type]),.appcfg input[type=text],.appcfg input[type=number]{width:100%}.appcfg select{max-width:260px;width:auto}.appcfg .acrow{display:flex;gap:7px;align-items:center;max-width:100%}.appcfg .acrow input{flex:1 1 auto;min-width:0}.appcfg .acrow input[type=number]{flex:0 1 14ch;width:14ch}.appcfg .unit{color:#64707d;font-size:13px;flex:0 0 auto}.appcfg .help{color:#64707d;font-size:13px;margin:4px 0 0}.appcfg .restart{color:#8a5a00;font-size:13px}.appcfg .confirmbox{display:none;border:1px solid #d8e0e6;background:#f8faf9;border-radius:6px;padding:10px;margin:10px 0;overflow-x:auto}.appcfg .confirmbox table,.appcfg .pendingbox table{border-collapse:collapse;width:100%;font-size:13px}.appcfg .confirmbox th,.appcfg .confirmbox td,.appcfg .pendingbox th,.appcfg .pendingbox td{border-bottom:1px solid #d8e0e6;padding:5px;text-align:left;vertical-align:top}.appcfg .confirmbox tr:last-child td,.appcfg .pendingbox tr:last-child td{border-bottom:0}.appcfg .acgroup th{background:#eef3f1;color:#64707d;font-size:12px;font-weight:700}.appcfg .pendingbox{border-color:#e1c46d;background:#fffdf6;overflow-x:auto}@media(max-width:640px){.appcfg{max-width:none}.appcfg select{width:100%;max-width:none}.appcfg .acrow{align-items:flex-start}.appcfg .acrow input[type=number]{max-width:100%}.appcfg .unit{padding-top:7px}}"
    "</style>";
#endif

void flushChunkBuffer();
bool sendResponseContent(const char* data, size_t len);
void markRequest();
void sendChunk(const char* data, size_t len);
void sendChunk(const char* text);
void sendEscapedHtmlChunk(const char* text);
void formatReadableBytes(uint64_t bytes, char* out, size_t len);
bool ensureAuth();
size_t chunkBufferSpace();

Esp32BaseWeb::Method fromHttpMethod(HTTPMethod method) {
    switch (method) {
        case HTTP_GET: return Esp32BaseWeb::METHOD_GET;
        case HTTP_POST: return Esp32BaseWeb::METHOD_POST;
        default: return Esp32BaseWeb::METHOD_UNKNOWN;
    }
}

const char* methodName(Esp32BaseWeb::Method method) {
    switch (method) {
        case Esp32BaseWeb::METHOD_GET: return "GET";
        case Esp32BaseWeb::METHOD_POST: return "POST";
        case Esp32BaseWeb::METHOD_ANY: return "ANY";
        case Esp32BaseWeb::METHOD_UNKNOWN:
        default: return "UNKNOWN";
    }
}

uint8_t routeCount(bool appPageOnly) {
    uint8_t count = 0;
    for (uint8_t i = 0; i < ESP32BASE_WEB_MAX_ROUTES; ++i) {
        if (g_routes[i].handler && (!appPageOnly || g_routes[i].appPage)) {
            ++count;
        }
    }
    return count;
}

bool validAuthUser(const char* value) {
    if (!value) {
        return false;
    }
    const size_t len = strlen(value);
    if (len == 0 || len >= sizeof(g_authUser)) {
        return false;
    }
    for (const char* p = value; *p; ++p) {
        const uint8_t c = static_cast<uint8_t>(*p);
        if (c <= 0x20 || c >= 0x7f || c == ':') {
            return false;
        }
    }
    return true;
}

bool validAuthPass(const char* value) {
    if (!value) {
        return false;
    }
    const size_t len = strlen(value);
    if (len == 0 || len > 63) {
        return false;
    }
    for (const char* p = value; *p; ++p) {
        const uint8_t c = static_cast<uint8_t>(*p);
        if (c <= 0x20 || c >= 0x7f) {
            return false;
        }
    }
    return true;
}

bool parseBasicAuth(char* user, size_t userLen, char* pass, size_t passLen) {
    if (!user || userLen == 0 || !pass || passLen == 0) {
        return false;
    }
    user[0] = '\0';
    pass[0] = '\0';
    const String header = g_server.header("Authorization");
    if (!header.startsWith("Basic ")) {
        return false;
    }
    if (header.length() > 180) {
        ESP32BASE_LOG_W("web", "auth_header_rejected reason=too_long length=%u", static_cast<unsigned>(header.length()));
        return false;
    }
    const char* encoded = header.c_str() + 6;
    uint8_t decoded[128];
    size_t decodedLen = 0;
    if (mbedtls_base64_decode(decoded, sizeof(decoded) - 1U, &decodedLen,
                              reinterpret_cast<const unsigned char*>(encoded), strlen(encoded)) != 0) {
        return false;
    }
    decoded[decodedLen] = '\0';
    char* colon = strchr(reinterpret_cast<char*>(decoded), ':');
    if (!colon) {
        return false;
    }
    *colon = '\0';
    const char* decodedUser = reinterpret_cast<char*>(decoded);
    const char* decodedPass = colon + 1;
    if (strlen(decodedUser) >= userLen || strlen(decodedPass) >= passLen) {
        return false;
    }
    strlcpy(user, decodedUser, userLen);
    strlcpy(pass, decodedPass, passLen);
    return true;
}

bool authMatches(const char* user, const char* pass) {
    if (!user || !pass || strcmp(user, g_authUser) != 0) {
        return false;
    }
    return strcmp(pass, g_authPass) == 0;
}

bool parseAndCheckAuth(const char* context) {
    char user[32];
    char pass[64];
    if (!parseBasicAuth(user, sizeof(user), pass, sizeof(pass))) {
        if (!g_authLoggedForRequest) {
            g_authLoggedForRequest = true;
            ESP32BASE_LOG_W("web", "auth_request context=%s result=missing_or_invalid", context ? context : "unknown");
        }
        return false;
    }
    const bool ok = authMatches(user, pass);
    if (!g_authLoggedForRequest) {
        g_authLoggedForRequest = true;
        if (!ok) {
            ESP32BASE_LOG_W("web", "auth_request context=%s user=%s password=%s result=failed",
                            context ? context : "unknown",
                            user,
                            pass);
        }
    }
    return ok;
}

void applyPlainAuth(const char* user, const char* pass) {
    strlcpy(g_authUser, user, sizeof(g_authUser));
    strlcpy(g_authPass, pass, sizeof(g_authPass));
    g_authLoadedFromStorage = false;
}

void applyDefaultAuth() {
    applyPlainAuth(g_defaultAuthSet ? g_defaultAuthUser : "admin",
                   g_defaultAuthSet ? g_defaultAuthPass : "admin");
}

void applyStoredAuth(const char* user, const char* pass) {
    strlcpy(g_authUser, user, sizeof(g_authUser));
    strlcpy(g_authPass, pass, sizeof(g_authPass));
    g_authLoadedFromStorage = true;
}

bool loadStoredAuth() {
    char user[32];
    char pass[64];
    Preferences prefs;
    if (!prefs.begin("eb_web", true)) {
        return false;
    }
    const bool hasUser = prefs.isKey("auth_user");
    const bool hasPass = prefs.isKey("auth_pass");
    if (!hasUser || !hasPass) {
        prefs.end();
        return false;
    }
    const size_t userLen = prefs.getString("auth_user", user, sizeof(user));
    const size_t passLen = prefs.getString("auth_pass", pass, sizeof(pass));
    prefs.end();
    if (userLen == 0 || passLen == 0) {
        return false;
    }
    if (!validAuthUser(user) || !validAuthPass(pass)) {
        ESP32BASE_LOG_W("web", "auth_load_failed reason=invalid_storage");
        return false;
    }
    applyStoredAuth(user, pass);
    ESP32BASE_LOG_I("web", "auth_loaded user=%s password=%s source=stored", g_authUser, g_authPass);
    return true;
}

bool saveStoredAuth(const char* user, const char* pass) {
    Preferences prefs;
    if (!prefs.begin("eb_web", false)) {
        return false;
    }
    const bool ok = prefs.putString("auth_user", user) > 0 &&
                    prefs.putString("auth_pass", pass) > 0;
    if (!ok) {
        prefs.remove("auth_user");
        prefs.remove("auth_pass");
    }
    prefs.end();
    return ok;
}

bool validHeaderValue(const char* value, size_t maxLen) {
    if (!value || !value[0]) {
        return false;
    }
    const size_t len = strlen(value);
    if (len > maxLen) {
        return false;
    }
    for (const char* p = value; *p; ++p) {
        const uint8_t c = static_cast<uint8_t>(*p);
        if (c < 0x20 || c == 0x7f) {
            return false;
        }
    }
    return true;
}

bool sameHost(const char* a, const char* b) {
    if (!a || !b || !a[0] || !b[0]) {
        return false;
    }
    while (*a && *b) {
        char ca = *a++;
        char cb = *b++;
        if (ca >= 'A' && ca <= 'Z') {
            ca = static_cast<char>(ca - 'A' + 'a');
        }
        if (cb >= 'A' && cb <= 'Z') {
            cb = static_cast<char>(cb - 'A' + 'a');
        }
        if (ca != cb) {
            return false;
        }
    }
    return *a == '\0' && *b == '\0';
}

bool extractUrlHost(const char* url, char* out, size_t len) {
    if (!url || !out || len == 0) {
        return false;
    }
    out[0] = '\0';
    const char* p = nullptr;
    if (strncmp(url, "http://", 7) == 0) {
        p = url + 7;
    } else if (strncmp(url, "https://", 8) == 0) {
        p = url + 8;
    } else {
        return false;
    }
    const char* end = p;
    while (*end && *end != '/' && *end != '?' && *end != '#') {
        ++end;
    }
    const char* at = static_cast<const char*>(memchr(p, '@', static_cast<size_t>(end - p)));
    if (at) {
        p = at + 1;
    }
    const size_t hostLen = static_cast<size_t>(end - p);
    if (hostLen == 0 || hostLen >= len) {
        return false;
    }
    memcpy(out, p, hostLen);
    out[hostLen] = '\0';
    return true;
}

bool requestSameOrigin() {
    const String origin = g_server.header("Origin");
    const String referer = g_server.header("Referer");
    if (origin.length() == 0 && referer.length() == 0) {
        return true;
    }

    const String hostHeader = g_server.header("Host");
    char host[96];
    strlcpy(host, hostHeader.c_str(), sizeof(host));
    if (!host[0]) {
        return false;
    }

    char sourceHost[96];
    if (origin.length() > 0) {
        if (!extractUrlHost(origin.c_str(), sourceHost, sizeof(sourceHost))) {
            return false;
        }
        return sameHost(host, sourceHost);
    }
    if (!extractUrlHost(referer.c_str(), sourceHost, sizeof(sourceHost))) {
        return false;
    }
    return sameHost(host, sourceHost);
}

bool ensurePostAllowed(const char* context) {
    if (!ensureAuth()) {
        return false;
    }
    if (requestSameOrigin()) {
        return true;
    }
    ESP32BASE_LOG_W("web", "post_rejected context=%s reason=cross_origin", context ? context : "unknown");
    g_server.send(403, "text/plain", "Forbidden");
    return false;
}

bool validDownloadFilename(const char* filename) {
    if (!filename || !filename[0] || strlen(filename) > 63) {
        return false;
    }
    for (const char* p = filename; *p; ++p) {
        const char c = *p;
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
        if (!ok) {
            return false;
        }
    }
    return true;
}

bool appendQuotedHeaderValue(char* out, size_t len, const char* value) {
    if (!out || len == 0 || !value) {
        return false;
    }
    size_t used = strlen(out);
    for (const char* p = value; *p; ++p) {
        const char c = *p;
        if (c == '"' || c == '\\') {
            if (used + 2 >= len) {
                return false;
            }
            out[used++] = '\\';
            out[used++] = c;
        } else {
            if (used + 1 >= len) {
                return false;
            }
            out[used++] = c;
        }
    }
    out[used] = '\0';
    return true;
}

uint8_t appPageCount() {
    uint8_t count = 0;
    for (uint8_t i = 0; i < ESP32BASE_WEB_MAX_ROUTES; ++i) {
        if (g_routes[i].handler && g_routes[i].appPage) {
            ++count;
        }
    }
    return count;
}

uint8_t navItemCount() {
    uint8_t count = 0;
    for (uint8_t i = 0; i < ESP32BASE_WEB_MAX_NAV_ITEMS; ++i) {
        if (g_navItems[i].path[0]) {
            ++count;
        }
    }
    return count;
}

bool navPathExists(const char* path) {
    if (!path || !path[0]) {
        return false;
    }
    for (uint8_t i = 0; i < ESP32BASE_WEB_MAX_NAV_ITEMS; ++i) {
        if (g_navItems[i].path[0] && strcmp(g_navItems[i].path, path) == 0) {
            return true;
        }
    }
    return false;
}

uint8_t appNavCount() {
    uint8_t count = navItemCount();
    for (uint8_t i = 0; i < ESP32BASE_WEB_MAX_ROUTES; ++i) {
        if (g_routes[i].handler && g_routes[i].appPage && !navPathExists(g_routes[i].path)) {
            ++count;
        }
    }
    return count;
}

const char* configuredHomePath() {
    if (g_homePath[0]) {
        return g_homePath;
    }
    for (uint8_t i = 0; i < ESP32BASE_WEB_MAX_NAV_ITEMS; ++i) {
        if (g_navItems[i].path[0]) {
            return g_navItems[i].path;
        }
    }
    for (uint8_t i = 0; i < ESP32BASE_WEB_MAX_ROUTES; ++i) {
        if (g_routes[i].handler && g_routes[i].appPage) {
            return g_routes[i].path;
        }
    }
    return "/esp32base";
}

bool useAppHome() {
    return g_homeMode == Esp32BaseWeb::HOME_APP && strcmp(configuredHomePath(), "/esp32base") != 0;
}

bool navPathMatches(const char* navPath, const char* currentPath) {
    if (!navPath || !navPath[0] || !currentPath || !currentPath[0]) {
        return false;
    }
    const size_t navLen = strlen(navPath);
    if (strncmp(currentPath, navPath, navLen) != 0) {
        return false;
    }
    if (currentPath[navLen] == '\0') {
        return true;
    }
    return navLen > 1 && currentPath[navLen] == '/';
}

void updateActivePath(const char* candidate, const char* currentPath, const char*& activePath, size_t& activeLen) {
    if (!navPathMatches(candidate, currentPath)) {
        return;
    }
    const size_t len = strlen(candidate);
    if (len > activeLen) {
        activePath = candidate;
        activeLen = len;
    }
}

const char* activeNavPath(bool includeSystemLinks) {
    const char* activePath = nullptr;
    size_t activeLen = 0;
    const char* currentPath = g_activeUri[0] ? g_activeUri : "/";
    updateActivePath(configuredHomePath(), currentPath, activePath, activeLen);
    if (g_homeMode != Esp32BaseWeb::HOME_ESP32BASE) {
        for (uint8_t i = 0; i < ESP32BASE_WEB_MAX_NAV_ITEMS; ++i) {
            if (g_navItems[i].path[0]) {
                updateActivePath(g_navItems[i].path, currentPath, activePath, activeLen);
            }
        }
        for (uint8_t i = 0; i < ESP32BASE_WEB_MAX_ROUTES; ++i) {
            if (g_routes[i].handler && g_routes[i].appPage && !navPathExists(g_routes[i].path)) {
                updateActivePath(g_routes[i].path, currentPath, activePath, activeLen);
            }
        }
    }
    if (includeSystemLinks) {
        updateActivePath("/esp32base", currentPath, activePath, activeLen);
        if (navPathMatches("/esp32base/wifi", currentPath) ||
            navPathMatches("/esp32base/auth", currentPath)
#if ESP32BASE_ENABLE_OTA
            || navPathMatches("/esp32base/ota", currentPath)
#endif
        ) {
            activePath = "/esp32base/tools";
            activeLen = strlen(activePath);
        }
        updateActivePath("/esp32base/logs", currentPath, activePath, activeLen);
#if ESP32BASE_ENABLE_APP_CONFIG
        updateActivePath("/esp32base/app-config", currentPath, activePath, activeLen);
#endif
        updateActivePath("/esp32base/tools", currentPath, activePath, activeLen);
    }
    return activePath;
}

void sendLink(const char* path, const char* title, bool paragraph, const char* extraClass = nullptr) {
    sendChunk(paragraph ? "<p><a href='" : "<a href='");
    sendEscapedHtmlChunk(path);
    sendChunk("'");
    if (extraClass && extraClass[0]) {
        sendChunk(" class='");
        sendEscapedHtmlChunk(extraClass);
        sendChunk("'");
    }
    sendChunk(">");
    sendEscapedHtmlChunk(title);
    sendChunk(paragraph ? "</a></p>" : "</a>");
}

void sendNavLink(const char* path, const char* title, bool paragraph, const char* activePath, const char* baseClass = nullptr) {
    char className[24] = "";
    if (baseClass && baseClass[0]) {
        strlcpy(className, baseClass, sizeof(className));
    }
    if (activePath && strcmp(activePath, path) == 0) {
        if (className[0]) {
            strlcat(className, " active", sizeof(className));
        } else {
            strlcpy(className, "active", sizeof(className));
        }
    }
    sendLink(path, title, paragraph, className[0] ? className : nullptr);
}

void sendInfoRowStart(const char* label) {
    sendChunk("<tr><th>");
    sendEscapedHtmlChunk(label);
    sendChunk("</th><td>");
}

void sendInfoRowEnd() {
    sendChunk("</td></tr>");
}

void sendInfoRow(const char* label, const char* value) {
    sendInfoRowStart(label);
    sendEscapedHtmlChunk(value && value[0] ? value : "-");
    sendInfoRowEnd();
}

void sendStatusSectionStart(const char* title) {
    sendChunk("<section class='panel statuspage'><h2>");
    sendEscapedHtmlChunk(title);
    sendChunk("</h2><table class='kv'>");
}

void sendStatusSectionEnd() {
    sendChunk("</table></section>");
}

#if ESP32BASE_ENABLE_WATCHDOG
struct WatchdogTripState {
    uint32_t lifetime;
    uint32_t base;
    uint32_t trip;
    uint32_t resetTime;
    bool hasBase;
    bool invalidBase;
};

WatchdogTripState readWatchdogTripState() {
    WatchdogTripState state = {};
    state.lifetime = Esp32BaseWatchdog::lifetimeResetCount();
    const int32_t base = Esp32BaseConfig::getInt("eb_sys", "wdt_trip_base", -1);
    state.hasBase = base >= 0;
    state.base = state.hasBase ? static_cast<uint32_t>(base) : 0;
    state.resetTime = static_cast<uint32_t>(Esp32BaseConfig::getInt("eb_sys", "wdt_trip_time", 0));
    state.invalidBase = state.hasBase && state.base > state.lifetime;
    state.trip = (!state.invalidBase && state.lifetime >= state.base) ? state.lifetime - state.base : 0;
    return state;
}

bool formatEpochTime(uint32_t epoch, char* out, size_t len) {
    if (!out || len == 0 || epoch == 0) {
        return false;
    }
    const time_t raw = static_cast<time_t>(epoch);
    struct tm tmValue;
    localtime_r(&raw, &tmValue);
    return strftime(out, len, "%Y-%m-%d %H:%M:%S", &tmValue) > 0;
}

void formatWatchdogTripResetAt(const WatchdogTripState& state, char* out, size_t len) {
    if (!out || len == 0) {
        return;
    }
    if (!state.hasBase) {
        strlcpy(out, "never", len);
        return;
    }
    if (state.invalidBase) {
        strlcpy(out, "invalid baseline", len);
        return;
    }
#if ESP32BASE_ENABLE_NTP
    if (state.resetTime >= ESP32BASE_NTP_SYNC_MIN_EPOCH && formatEpochTime(state.resetTime, out, len)) {
        return;
    }
#endif
    strlcpy(out, "unknown (time unavailable)", len);
}

uint32_t currentWatchdogTripResetTime() {
#if ESP32BASE_ENABLE_NTP
    const Esp32BaseNtp::TimeSnapshot time = Esp32BaseNtp::snapshot();
    if (time.synced) {
        return time.epochSec;
    }
#endif
    return 0;
}
#endif

void formatMac(uint64_t mac, char* out, size_t len) {
    if (!out || len == 0) {
        return;
    }
    snprintf(out, len, "%02X:%02X:%02X:%02X:%02X:%02X",
             static_cast<unsigned>(mac & 0xff),
             static_cast<unsigned>((mac >> 8) & 0xff),
             static_cast<unsigned>((mac >> 16) & 0xff),
             static_cast<unsigned>((mac >> 24) & 0xff),
             static_cast<unsigned>((mac >> 32) & 0xff),
             static_cast<unsigned>((mac >> 40) & 0xff));
}

const char* partitionTypeName(esp_partition_type_t type) {
    switch (type) {
        case ESP_PARTITION_TYPE_APP: return "app";
        case ESP_PARTITION_TYPE_DATA: return "data";
        default: return "unknown";
    }
}

const char* partitionSubtypeName(const esp_partition_t* partition) {
    if (!partition) {
        return "unknown";
    }
    if (partition->type == ESP_PARTITION_TYPE_APP) {
        if (partition->subtype >= ESP_PARTITION_SUBTYPE_APP_OTA_MIN &&
            partition->subtype <= ESP_PARTITION_SUBTYPE_APP_OTA_MAX) {
            return partition->label;
        }
        switch (partition->subtype) {
            case ESP_PARTITION_SUBTYPE_APP_FACTORY: return "factory";
            case ESP_PARTITION_SUBTYPE_APP_TEST: return "test";
            default: return "app";
        }
    }
    if (partition->type == ESP_PARTITION_TYPE_DATA) {
        switch (partition->subtype) {
            case ESP_PARTITION_SUBTYPE_DATA_NVS: return "nvs";
            case ESP_PARTITION_SUBTYPE_DATA_PHY: return "phy";
            case ESP_PARTITION_SUBTYPE_DATA_OTA: return "ota";
            case ESP_PARTITION_SUBTYPE_DATA_SPIFFS: return "spiffs";
            case ESP_PARTITION_SUBTYPE_DATA_COREDUMP: return "coredump";
            default: return "data";
        }
    }
    return "unknown";
}

bool samePartition(const esp_partition_t* a, const esp_partition_t* b) {
    return a && b && a->address == b->address && a->size == b->size &&
           a->type == b->type && a->subtype == b->subtype &&
           strncmp(a->label, b->label, sizeof(a->label)) == 0;
}

const char* partitionRole(const esp_partition_t* partition, const esp_partition_t* running, const esp_partition_t* nextOta) {
    if (!partition) {
        return "-";
    }
    if (samePartition(partition, running)) {
        return "running app";
    }
    if (samePartition(partition, nextOta)) {
        return "next OTA";
    }
    if (partition->type == ESP_PARTITION_TYPE_APP) {
        return "app";
    }
    if (partition->type == ESP_PARTITION_TYPE_DATA) {
        switch (partition->subtype) {
            case ESP_PARTITION_SUBTYPE_DATA_NVS: return "NVS config";
            case ESP_PARTITION_SUBTYPE_DATA_OTA: return "OTA state";
            case ESP_PARTITION_SUBTYPE_DATA_SPIFFS: return "app data";
            case ESP_PARTITION_SUBTYPE_DATA_COREDUMP: return "coredump";
            default: return "data";
        }
    }
    return "-";
}

void sendPartitionTable() {
    const esp_partition_t* running = esp_ota_get_running_partition();
    const esp_partition_t* nextOta = esp_ota_get_next_update_partition(nullptr);
    esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, nullptr);
    sendChunk("<section class='panel statuspage'><h2>Partition Table</h2><table class='part'><tr><th>Name</th><th>Type</th><th>SubType</th><th>Offset</th><th>Size</th><th>Role</th></tr>");
    while (it) {
        const esp_partition_t* p = esp_partition_get(it);
        if (p) {
            char offset[16];
            char sizeBuf[48];
            snprintf(offset, sizeof(offset), "0x%06lx", static_cast<unsigned long>(p->address));
            formatReadableBytes(p->size, sizeBuf, sizeof(sizeBuf));
            sendChunk("<tr><td>");
            sendEscapedHtmlChunk(p->label);
            sendChunk("</td><td>");
            sendEscapedHtmlChunk(partitionTypeName(p->type));
            sendChunk("</td><td>");
            sendEscapedHtmlChunk(partitionSubtypeName(p));
            sendChunk("</td><td>");
            sendEscapedHtmlChunk(offset);
            sendChunk("</td><td>");
            sendEscapedHtmlChunk(sizeBuf);
            sendChunk("</td><td>");
            sendEscapedHtmlChunk(partitionRole(p, running, nextOta));
            sendChunk("</td></tr>");
        }
        it = esp_partition_next(it);
    }
    esp_partition_iterator_release(it);
    sendChunk("</table></section>");
}

void sendAppLinks(bool paragraph, const char* activePath) {
    const char* home = configuredHomePath();
    for (uint8_t i = 0; i < ESP32BASE_WEB_MAX_NAV_ITEMS; ++i) {
        if (g_navItems[i].path[0] && (paragraph || strcmp(g_navItems[i].path, home) != 0)) {
            sendNavLink(g_navItems[i].path, g_navItems[i].title, paragraph, activePath);
        }
    }
    for (uint8_t i = 0; i < ESP32BASE_WEB_MAX_ROUTES; ++i) {
        if (!g_routes[i].handler || !g_routes[i].appPage || navPathExists(g_routes[i].path)) {
            continue;
        }
        if (!paragraph && strcmp(g_routes[i].path, home) == 0) {
            continue;
        }
        sendNavLink(g_routes[i].path, g_routes[i].title[0] ? g_routes[i].title : g_routes[i].path, paragraph, activePath);
    }
}

void sendSystemLinks(bool paragraph, const char* activePath = nullptr) {
    sendNavLink("/esp32base", g_builtinLabels[Esp32BaseWeb::BUILTIN_HOME], paragraph, activePath);
    sendNavLink("/esp32base/logs", g_builtinLabels[Esp32BaseWeb::BUILTIN_LOGS], paragraph, activePath);
#if ESP32BASE_ENABLE_APP_CONFIG
    sendNavLink("/esp32base/app-config", "App Config", paragraph, activePath);
#endif
    sendNavLink("/esp32base/tools", g_builtinLabels[Esp32BaseWeb::BUILTIN_TOOLS], paragraph, activePath);
}

void sendMainNav() {
    const char* activePath = activeNavPath(g_systemNavMode == Esp32BaseWeb::SYSTEM_NAV_TOP);
    sendChunk("<nav>");
    sendNavLink(configuredHomePath(), g_deviceName, false, activePath, "brand");
    if (g_homeMode != Esp32BaseWeb::HOME_ESP32BASE) {
        sendAppLinks(false, activePath);
    }
    if (g_systemNavMode == Esp32BaseWeb::SYSTEM_NAV_TOP) {
        sendSystemLinks(false, activePath);
    }
    sendChunk("</nav>");
}

void sendSystemNavSection() {
    if (g_systemNavMode == Esp32BaseWeb::SYSTEM_NAV_BOTTOM) {
        sendChunk("<nav>");
        sendSystemLinks(false, nullptr);
        sendChunk("</nav>");
    }
}

void formatReadableBytes(uint64_t bytes, char* out, size_t len) {
    if (!out || len == 0) {
        return;
    }
    if (bytes >= 1024ULL * 1024ULL) {
        snprintf(out, len, "%.2f MB", static_cast<double>(bytes) / 1024.0 / 1024.0);
    } else if (bytes >= 1024ULL) {
        snprintf(out, len, "%.2f KB", static_cast<double>(bytes) / 1024.0);
    } else {
        snprintf(out, len, "%llu B", static_cast<unsigned long long>(bytes));
    }
}

void sendFooterStats() {
    char heap[48];
    char uptime[48];
    formatReadableBytes(Esp32BaseSystem::freeHeap(), heap, sizeof(heap));
    Esp32BaseLog::formatUptime(Esp32BaseSystem::uptimeMs(), uptime, sizeof(uptime));
    sendChunk("Free heap: ");
    sendEscapedHtmlChunk(heap);
    sendChunk(" · Up: ");
    sendEscapedHtmlChunk(uptime);
    sendChunk(" · RSSI: ");
    if (Esp32BaseWiFi::isConnected()) {
        char rssi[24];
        snprintf(rssi, sizeof(rssi), "%ld dBm", static_cast<long>(Esp32BaseWiFi::rssi()));
        sendEscapedHtmlChunk(rssi);
    } else {
        sendChunk("-");
    }
}

void sendProgmem(const char* p) {
    if (!p || !g_responseActive || g_responseBroken) {
        return;
    }
    while (true) {
        const size_t space = chunkBufferSpace();
        if (space == 0) {
            flushChunkBuffer();
            if (g_responseBroken) {
                return;
            }
            continue;
        }

        char* dest = g_chunkBuffer + g_chunkUsed;
        size_t taken = 0;
        while (taken < space) {
            const char c = static_cast<char>(pgm_read_byte(p++));
            if (!c) {
                g_chunkUsed += taken;
                return;
            }
            dest[taken++] = c;
        }
        g_chunkUsed += taken;
    }
}

HTTPMethod toHttpMethod(Esp32BaseWeb::Method method) {
    switch (method) {
        case Esp32BaseWeb::METHOD_POST: return HTTP_POST;
        case Esp32BaseWeb::METHOD_ANY: return HTTP_ANY;
        case Esp32BaseWeb::METHOD_GET:
        default: return HTTP_GET;
    }
}

bool ensureAuth() {
    if (!g_activeUri[0]) {
        markRequest();
    }
    if (!g_authEnabled) {
        return true;
    }
    if (parseAndCheckAuth("check")) {
        return true;
    }
    g_server.requestAuthentication();
    return false;
}

bool isAuthenticated() {
    if (!g_authEnabled) {
        return true;
    }
    return parseAndCheckAuth("verify");
}

void registerRoute(Route& route) {
    Route* routePtr = &route;
    g_server.on(route.path, toHttpMethod(route.method), [routePtr]() {
        g_requestContextActive = true;
        markRequest();
        if (routePtr->handler) {
            routePtr->handler();
        }
        g_responseActive = false;
        g_requestContextActive = false;
        g_currentMethod = Esp32BaseWeb::METHOD_UNKNOWN;
    });
    route.registered = true;
}

bool beginResponse(int code, const char* contentType, const char* filename) {
    if (!g_requestContextActive) {
        ESP32BASE_LOG_W("web", "begin_response outside request");
        return false;
    }
    if (!validHeaderValue(contentType, 63)) {
        ESP32BASE_LOG_W("web", "begin_response invalid content_type");
        return false;
    }
    if (filename && filename[0]) {
        if (!validDownloadFilename(filename)) {
            ESP32BASE_LOG_W("web", "begin_response invalid filename");
            return false;
        }
        char disposition[96];
        strlcpy(disposition, "attachment; filename=\"", sizeof(disposition));
        if (!appendQuotedHeaderValue(disposition, sizeof(disposition), filename) ||
            strlcat(disposition, "\"", sizeof(disposition)) >= sizeof(disposition)) {
            ESP32BASE_LOG_W("web", "begin_response filename too long");
            return false;
        }
        g_server.sendHeader("Content-Disposition", disposition);
    }
    g_chunkUsed = 0;
    g_responseActive = true;
    g_responseBroken = false;
    g_server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    g_server.send(code, contentType, "");
    yield();
    return true;
}

bool responseClientConnected() {
    return g_server.client().connected();
}

size_t chunkBufferSpace() {
    const size_t capacity = kResponseChunkPayloadMax < sizeof(g_chunkBuffer) ? kResponseChunkPayloadMax : sizeof(g_chunkBuffer) - 1U;
    return g_chunkUsed < capacity ? capacity - g_chunkUsed : 0U;
}

void markResponseClientDisconnected() {
    if (!g_responseBroken) {
        g_responseBroken = true;
        ESP32BASE_LOG_W("web", "response_client_disconnected uri=%s", g_activeUri[0] ? g_activeUri : "-");
    }
}

bool sendResponseContent(const char* data, size_t len) {
    if (g_responseBroken) {
        return false;
    }
    if (!responseClientConnected()) {
        markResponseClientDisconnected();
        return false;
    }
    g_server.sendContent(data, len);
    yield();
    return true;
}

void flushChunkBuffer() {
    if (g_chunkUsed == 0) {
        return;
    }
    if (g_responseBroken) {
        g_chunkUsed = 0;
        return;
    }
    if (!responseClientConnected()) {
        markResponseClientDisconnected();
        g_chunkUsed = 0;
        return;
    }
    g_chunkBuffer[g_chunkUsed] = '\0';
    sendResponseContent(g_chunkBuffer, g_chunkUsed);
    g_chunkUsed = 0;
}

void sendChunk(const char* data, size_t len) {
    if (!data || len == 0) {
        return;
    }
    if (!g_responseActive) {
        ESP32BASE_LOG_W("web", "send_chunk outside response");
        return;
    }
    if (g_responseBroken) {
        return;
    }
    while (len > 0) {
        const size_t space = chunkBufferSpace();
        if (space == 0) {
            flushChunkBuffer();
            if (g_responseBroken) {
                return;
            }
            continue;
        }
        size_t take = len;
        if (take > space) {
            take = space;
        }
        memcpy(g_chunkBuffer + g_chunkUsed, data, take);
        g_chunkUsed += take;
        data += take;
        len -= take;
    }
}

void sendChunk(const char* text) {
    if (!text) {
        return;
    }
    sendChunk(text, strlen(text));
}

void endResponse() {
    if (!g_responseActive) {
        ESP32BASE_LOG_W("web", "end_response outside response");
        return;
    }
    flushChunkBuffer();
    if (!g_responseBroken) {
        g_server.sendContent("");
        yield();
    }
    g_responseActive = false;
    g_responseBroken = false;
}

void markRequest() {
    const Esp32BaseWeb::Method method = fromHttpMethod(g_server.method());
    g_lastRequestMethod = method;
    g_requestContextActive = true;
    if (g_requestContextActive) {
        g_currentMethod = method;
    }
    strlcpy(g_activeUri, g_server.uri().c_str(), sizeof(g_activeUri));
}

void redirectSeeOther(const char* url) {
    g_server.sendHeader("Location", url, true);
    g_server.sendHeader("Cache-Control", "no-store");
    g_server.send(303, "text/plain", "");
}

void sendUintChunk(uint64_t value) {
    char buf[24];
    snprintf(buf, sizeof(buf), "%llu", static_cast<unsigned long long>(value));
    sendChunk(buf);
}

void sendIntChunk(int value) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", value);
    sendChunk(buf);
}

void sendEscapedJsonChunk(const char* text) {
    if (!text) {
        return;
    }
    for (const char* p = text; *p; ++p) {
        switch (*p) {
            case '"': sendChunk("\\\""); break;
            case '\\': sendChunk("\\\\"); break;
            case '\n': sendChunk("\\n"); break;
            case '\r': sendChunk("\\r"); break;
            case '\t': sendChunk("\\t"); break;
            default:
                if (static_cast<uint8_t>(*p) < 0x20) {
                    char buf[7];
                    snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned>(*p));
                    sendChunk(buf);
                } else {
                    char one[2] = {*p, '\0'};
                    sendChunk(one);
                }
                break;
        }
    }
}

void sendBytesJsonChunk(uint32_t bytes) {
    char human[48];
    Esp32BaseLog::formatBytes(bytes, human, sizeof(human));
    sendChunk("{\"bytes\":");
    sendUintChunk(bytes);
    sendChunk(",\"human\":\"");
    sendEscapedJsonChunk(human);
    sendChunk("\"}");
}

void sendEscapedHtmlChunk(const char* text) {
    if (!text) {
        return;
    }
    for (const char* p = text; *p; ++p) {
        switch (*p) {
            case '&': sendChunk("&amp;"); break;
            case '<': sendChunk("&lt;"); break;
            case '>': sendChunk("&gt;"); break;
            case '"': sendChunk("&quot;"); break;
            case '\'': sendChunk("&#39;"); break;
            default: {
                char one[2] = {*p, '\0'};
                sendChunk(one);
                break;
            }
        }
    }
}

void handleStatus() {
    markRequest();
    if (!ensureAuth()) {
        return;
    }
    if (!beginResponse(200, "application/json", nullptr)) {
        return;
    }
    sendChunk("{\"firmware\":{\"name\":\"");
    sendEscapedJsonChunk(Esp32Base::firmwareName());
    sendChunk("\",\"version\":\"");
    sendEscapedJsonChunk(Esp32Base::firmwareVersion());
    sendChunk("\",\"build\":\"");
    sendEscapedJsonChunk(Esp32Base::firmwareBuild());
    sendChunk("\"},\"profile\":\"");
    sendEscapedJsonChunk(Esp32Base::profileName());
    sendChunk("\",\"hostname\":\"");
    sendEscapedJsonChunk(Esp32Base::hostname());
    sendChunk("\",\"heap\":{\"free\":");
    sendBytesJsonChunk(Esp32BaseSystem::freeHeap());
    sendChunk(",\"minFree\":");
    sendBytesJsonChunk(Esp32BaseSystem::minFreeHeap());
    sendChunk("},\"flash\":");
    sendBytesJsonChunk(Esp32BaseSystem::flashSize());
    sendChunk(",\"resetReason\":\"");
    sendEscapedJsonChunk(Esp32BaseSystem::resetReason());
    sendChunk("\",\"resetReasonText\":\"");
    sendEscapedJsonChunk(Esp32BaseSystem::resetReasonText());
    sendChunk("\",\"wakeReason\":\"");
    sendEscapedJsonChunk(Esp32BaseSystem::wakeReason());
    sendChunk("\",\"wakeReasonText\":\"");
    sendEscapedJsonChunk(Esp32BaseSystem::wakeReasonText());
    sendChunk("\"");
    sendChunk(",\"wifi\":{\"state\":\"");
    sendEscapedJsonChunk(Esp32BaseWiFi::stateName());
    sendChunk("\",\"connected\":");
    sendChunk(Esp32BaseWiFi::isConnected() ? "true" : "false");
    sendChunk(",\"rssi\":");
    sendIntChunk(Esp32BaseWiFi::isConnected() ? Esp32BaseWiFi::rssi() : 0);
    sendChunk("}}");
    endResponse();
}

void handleChip() {
    markRequest();
    if (!ensureAuth()) {
        return;
    }
    if (!beginResponse(200, "application/json", nullptr)) {
        return;
    }
    char mac[18];
    formatMac(ESP.getEfuseMac(), mac, sizeof(mac));
    sendChunk("{\"chipModel\":\"");
    sendEscapedJsonChunk(ESP.getChipModel());
    sendChunk("\",\"chipRevision\":");
    sendIntChunk(ESP.getChipRevision());
    sendChunk(",\"cpuMHz\":");
    sendIntChunk(ESP.getCpuFreqMHz());
    sendChunk(",\"sdkVersion\":\"");
    sendEscapedJsonChunk(ESP.getSdkVersion());
    sendChunk("\",\"efuseMac\":\"");
    sendEscapedJsonChunk(mac);
    sendChunk("\",\"staMac\":\"");
    sendEscapedJsonChunk(WiFi.macAddress().c_str());
    sendChunk("\",\"apMac\":\"");
    sendEscapedJsonChunk(WiFi.softAPmacAddress().c_str());
    sendChunk("\",\"freeHeap\":");
    sendBytesJsonChunk(Esp32BaseSystem::freeHeap());
    sendChunk(",\"minFreeHeap\":");
    sendBytesJsonChunk(Esp32BaseSystem::minFreeHeap());
    sendChunk(",\"totalHeap\":");
    sendBytesJsonChunk(Esp32BaseSystem::totalHeap());
    sendChunk(",\"psram\":{\"total\":");
    sendBytesJsonChunk(ESP.getPsramSize());
    sendChunk(",\"free\":");
    sendBytesJsonChunk(ESP.getFreePsram());
    sendChunk("}");
    sendChunk(",\"flash\":");
    sendBytesJsonChunk(Esp32BaseSystem::flashSize());
    sendChunk(",\"resetReason\":\"");
    sendEscapedJsonChunk(Esp32BaseSystem::resetReason());
    sendChunk("\",\"resetReasonText\":\"");
    sendEscapedJsonChunk(Esp32BaseSystem::resetReasonText());
    sendChunk("\",\"wakeReason\":\"");
    sendEscapedJsonChunk(Esp32BaseSystem::wakeReason());
    sendChunk("\",\"wakeReasonText\":\"");
    sendEscapedJsonChunk(Esp32BaseSystem::wakeReasonText());
    sendChunk("\"}");
    endResponse();
}

void handleFirmware() {
    markRequest();
    if (!ensureAuth()) {
        return;
    }
    if (!beginResponse(200, "application/json", nullptr)) {
        return;
    }
    sendChunk("{\"name\":\"");
    sendEscapedJsonChunk(Esp32Base::firmwareName());
    sendChunk("\",\"version\":\"");
    sendEscapedJsonChunk(Esp32Base::firmwareVersion());
    sendChunk("\",\"build\":\"");
    sendEscapedJsonChunk(Esp32Base::firmwareBuild());
    sendChunk("\"}");
    endResponse();
}

bool loadStoredHostname(char* out, size_t len) {
    if (!out || len == 0) {
        return false;
    }
    out[0] = '\0';
    return Esp32BaseConfig::getStr("eb_sys", "hostname", out, len, "");
}

bool hostnameRestartRequired(const char* storedHostname) {
    return storedHostname && storedHostname[0] &&
           Esp32Base::isValidHostname(storedHostname) &&
           strcmp(storedHostname, Esp32Base::hostname()) != 0;
}

void sendHostnameJson(int code) {
    char stored[64] = "";
    const bool hasStored = loadStoredHostname(stored, sizeof(stored));
    const bool storedValid = hasStored && Esp32Base::isValidHostname(stored);
    if (!beginResponse(code, "application/json", nullptr)) {
        return;
    }
    sendChunk("{\"currentHostname\":\"");
    sendEscapedJsonChunk(Esp32Base::hostname());
    sendChunk("\",\"defaultHostname\":\"");
    sendEscapedJsonChunk(Esp32Base::defaultHostname());
    sendChunk("\",\"storedHostname\":\"");
    sendEscapedJsonChunk(hasStored ? stored : "");
    sendChunk("\",\"storedValid\":");
    sendChunk(storedValid ? "true" : "false");
    sendChunk(",\"restartRequired\":");
    sendChunk(hostnameRestartRequired(stored) ? "true" : "false");
    sendChunk(",\"rule\":\"1-32 lowercase letters, digits and hyphen; no leading/trailing hyphen; no .local\"}");
    endResponse();
}

void handleHostnameApiGet() {
    markRequest();
    if (!ensureAuth()) {
        return;
    }
    sendHostnameJson(200);
}

void handleHostnameSubmit() {
    markRequest();
    if (!ensurePostAllowed("hostname_submit")) {
        return;
    }
    const String hostname = g_server.arg("hostname");
    const String uri = g_server.uri();
    const bool apiRequest = strcmp(uri.c_str(), "/esp32base/api/hostname") == 0;
    if (!Esp32Base::isValidHostname(hostname.c_str())) {
        ESP32BASE_LOG_W("web", "hostname_save_rejected value=%s", hostname.c_str());
        if (apiRequest) {
            sendHostnameJson(400);
        } else {
            redirectSeeOther("/esp32base/tools?hostname_error=invalid");
        }
        return;
    }
    const bool ok = Esp32BaseConfig::setStr("eb_sys", "hostname", hostname.c_str());
    ESP32BASE_LOG_I("web", "hostname_save value=%s current=%s restart_required=%s result=%s",
                    hostname.c_str(),
                    Esp32Base::hostname(),
                    strcmp(hostname.c_str(), Esp32Base::hostname()) != 0 ? "yes" : "no",
                    ok ? "success" : "failed");
    if (apiRequest) {
        sendHostnameJson(ok ? 200 : 500);
    } else {
        redirectSeeOther(ok ? "/esp32base/tools?hostname_saved=1" : "/esp32base/tools?hostname_error=save_failed");
    }
}

void handleWifiPage() {
    markRequest();
    if (!ensureAuth()) {
        return;
    }
    char ssid[33] = "";
    char password[65] = "";
    Esp32BaseConfig::getStr("eb_wifi", "ssid", ssid, sizeof(ssid), Esp32BaseWiFi::ssid());
    Esp32BaseConfig::getStr("eb_wifi", "pass", password, sizeof(password), "");

    Esp32BaseWeb::sendHeader(g_builtinLabels[Esp32BaseWeb::BUILTIN_WIFI]);
    if (g_server.hasArg("saved")) {
        sendChunk("<p class=ok>Saved. Credentials updated and connection started.</p>");
    } else if (g_server.hasArg("error")) {
        if (g_server.arg("error") == "clear_failed") {
            sendChunk("<p class=err>WiFi credentials were not cleared.</p>");
        } else {
            sendChunk("<p class=err>WiFi settings were not saved.</p>");
        }
    } else if (g_server.hasArg("cleared")) {
        sendChunk("<p class=ok>Saved WiFi credentials were cleared.</p>");
    }
    sendChunk("<header class='pagehead'><h1>WiFi Settings</h1></header><section class='panel'><form method='post' action='/esp32base/wifi' onsubmit=\"if(!this.ssid.value.trim()){alert('SSID cannot be empty');return false;}if(this.ssid.value.length>32||this.password.value.length>64){alert('SSID or password is too long');return false;}return once(this);\">");
    sendChunk("<label for='ssid'>SSID</label><input id='ssid' name='ssid' value='");
    sendEscapedHtmlChunk(ssid);
    sendChunk("' maxlength='32' autocomplete='off'><label for='wp'>Password (optional)</label><input id='wp' type='password' name='password' value='");
    sendEscapedHtmlChunk(password);
    sendChunk("' maxlength='64'><div class='actions'><input type='button' value='Show/Hide Password' onclick=\"var p=document.getElementById('wp');p.type=p.type=='password'?'text':'password'\"><input type='submit' value='Save &amp; Connect'></div></form></section>");
    sendChunk("<section class='panel'><h2>Clear WiFi</h2><p class='muted'>Remove stored WiFi credentials from this device.</p><form method='post' action='/esp32base/api/wifi/clear' onsubmit=\"return confirm('Clear WiFi credentials?')&&once(this)\"><input class='danger' type='submit' value='Clear WiFi'></form></section>");
    Esp32BaseWeb::sendFooter();
}

void handleWifiSubmit() {
    markRequest();
    if (!ensurePostAllowed("wifi_submit")) {
        return;
    }
    const String ssid = g_server.arg("ssid");
    const String password = g_server.arg("password");
    if (ssid.length() == 0 || ssid.length() > 32 || password.length() > 64) {
        redirectSeeOther("/esp32base/wifi?error=invalid");
        return;
    }
    const bool ok = Esp32BaseConfig::setStr("eb_wifi", "ssid", ssid.c_str()) &&
                    Esp32BaseConfig::setStr("eb_wifi", "pass", password.c_str());
    ESP32BASE_LOG_I("web", "wifi form submitted ssid=%s password=%s result=%s",
                    ssid.c_str(), password.c_str(), ok ? "success" : "failed");
    redirectSeeOther(ok ? "/esp32base/wifi?saved=1" : "/esp32base/wifi?error=save_failed");
    if (ok) {
        delay(250);
        Esp32BaseWiFi::connect(ssid.c_str(), password.c_str(), false);
    }
}

void handleWifiClear() {
    markRequest();
    if (!ensurePostAllowed("wifi_clear")) {
        return;
    }
    const bool ok = Esp32BaseWiFi::clearCredentials();
    redirectSeeOther(ok ? "/esp32base/wifi?cleared=1" : "/esp32base/wifi?error=clear_failed");
}

#if ESP32BASE_ENABLE_FILELOG
void sendFileLogModeOption(const char* value, const char* label, Esp32BaseFileLog::Mode mode) {
    sendChunk("<label><input type='radio' name='mode' value='");
    sendEscapedHtmlChunk(value);
    sendChunk("'");
    if (Esp32BaseFileLog::mode() == mode) {
        sendChunk(" checked");
    }
    sendChunk("> ");
    sendEscapedHtmlChunk(label);
    sendChunk("</label> ");
}

bool fileLogModeFromArg(const String& raw, Esp32BaseFileLog::Mode& mode) {
    if (raw == "off") {
        mode = Esp32BaseFileLog::OFF;
        return true;
    }
#if ESP32BASE_LOG_LEVEL >= ESP32BASE_LOG_WARN
    if (raw == "warn") {
        mode = Esp32BaseFileLog::WARN;
        return true;
    }
#endif
#if ESP32BASE_LOG_LEVEL >= ESP32BASE_LOG_INFO
    if (raw == "info") {
        mode = Esp32BaseFileLog::INFO;
        return true;
    }
#endif
    return false;
}
#endif

#if ESP32BASE_ENABLE_WATCHDOG
void sendWatchdogPanel() {
    const WatchdogTripState state = readWatchdogTripState();
    char count[32];
    char resetAt[32];
    formatWatchdogTripResetAt(state, resetAt, sizeof(resetAt));
    snprintf(count, sizeof(count), "%lu",
             static_cast<unsigned long>(state.lifetime));
    sendChunk("<section class='panel'><h2>Watchdog trip</h2><table class='kv'>");
    sendInfoRow("Lifetime resets", count);
    if (state.invalidBase) {
        sendInfoRow("Trip resets", "invalid baseline");
    } else {
        snprintf(count, sizeof(count), "%lu",
                 static_cast<unsigned long>(state.trip));
        sendInfoRow("Trip resets", count);
    }
    sendInfoRow("Trip reset at", resetAt);
    sendChunk("</table><form method='post' action='/esp32base/tools/watchdog-trip-reset' onsubmit=\"return confirm('Reset Watchdog trip counter? Lifetime resets are kept.')&&once(this)\"><input type='submit' value='Reset Watchdog Trip'></form></section>");
}
#endif

#if ESP32BASE_ENABLE_APP_CONFIG
bool validConfigName(const char* value) {
    if (!value) {
        return false;
    }
    const size_t len = strlen(value);
    return len >= 1 && len <= 15;
}

bool appNsAllowed(const char* ns) {
    return validConfigName(ns) && strncmp(ns, "eb_", 3) != 0;
}

Esp32BaseAppConfig::FieldType publicFieldType(AppConfigStoredType type) {
    switch (type) {
        case APP_CFG_STRING: return Esp32BaseAppConfig::FIELD_STRING;
        case APP_CFG_INT: return Esp32BaseAppConfig::FIELD_INT;
        case APP_CFG_DECIMAL: return Esp32BaseAppConfig::FIELD_DECIMAL;
        case APP_CFG_BOOL: return Esp32BaseAppConfig::FIELD_BOOL;
        case APP_CFG_ENUM: return Esp32BaseAppConfig::FIELD_ENUM;
    }
    return Esp32BaseAppConfig::FIELD_STRING;
}

Esp32BaseAppConfig::FieldRef makeFieldRef(const AppConfigFieldSlot& field) {
    Esp32BaseAppConfig::FieldRef ref = {field.groupId, field.ns, field.key, field.label, publicFieldType(field.type), field.restartRequired};
    return ref;
}

bool groupExists(const char* groupId) {
    if (!groupId || !groupId[0]) {
        return false;
    }
    for (uint8_t i = 0; i < g_appConfigGroupCount; ++i) {
        if (strcmp(g_appConfigGroups[i].id, groupId) == 0) {
            return true;
        }
    }
    return false;
}

bool fieldExists(const char* ns, const char* key) {
    for (uint8_t i = 0; i < g_appConfigFieldCount; ++i) {
        if (strcmp(g_appConfigFields[i].ns, ns) == 0 && strcmp(g_appConfigFields[i].key, key) == 0) {
            return true;
        }
    }
    return false;
}

bool validFieldCommon(const char* groupId, const char* ns, const char* key, const char* label) {
    if (!groupExists(groupId) || !appNsAllowed(ns) || !validConfigName(key) || !label || !label[0] ||
        strlen(label) > Esp32BaseAppConfig::LABEL_MAX_LENGTH) {
        return false;
    }
    return !fieldExists(ns, key);
}

bool validOptionalHelp(const char* help) {
    return !help || strlen(help) <= Esp32BaseAppConfig::HELP_MAX_LENGTH;
}

bool validOptionalUnit(const char* unit) {
    return !unit || strlen(unit) <= Esp32BaseAppConfig::UNIT_MAX_LENGTH;
}

void appConfigLogRegisterFailed(const char* type, const char* ns, const char* key) {
    ESP32BASE_LOG_W("appcfg", "register_failed type=%s ns=%s key=%s", type ? type : "-", ns ? ns : "-", key ? key : "-");
}

bool enumValueAllowed(const Esp32BaseAppConfig::EnumField& field, const char* value) {
    if (!value) {
        return false;
    }
    for (uint8_t i = 0; i < field.optionCount; ++i) {
        if (field.options[i].value && strcmp(field.options[i].value, value) == 0) {
            return true;
        }
    }
    return false;
}

bool validEnumOptions(const Esp32BaseAppConfig::EnumField& field) {
    if (!field.options || field.optionCount == 0) {
        return false;
    }
    for (uint8_t i = 0; i < field.optionCount; ++i) {
        const char* value = field.options[i].value;
        const char* label = field.options[i].label;
        if (!value || !value[0] || strlen(value) > Esp32BaseAppConfig::ENUM_VALUE_MAX_LENGTH ||
            !label || !label[0] || strlen(label) > Esp32BaseAppConfig::LABEL_MAX_LENGTH) {
            return false;
        }
        for (uint8_t j = static_cast<uint8_t>(i + 1); j < field.optionCount; ++j) {
            if (field.options[j].value && strcmp(value, field.options[j].value) == 0) {
                return false;
            }
        }
    }
    return enumValueAllowed(field, field.defaultValue);
}

bool stepMatches(int32_t value, int32_t minValue, int32_t step) {
    if (step <= 0) {
        return false;
    }
    const int64_t diff = static_cast<int64_t>(value) - static_cast<int64_t>(minValue);
    return diff >= 0 && (diff % step) == 0;
}

bool parseStrictInt32(const char* text, int32_t& out) {
    if (!text || !text[0]) {
        return false;
    }
    char* end = nullptr;
    errno = 0;
    const long value = strtol(text, &end, 10);
    if (errno != 0 || !end || *end != '\0' || value < INT32_MIN || value > INT32_MAX) {
        return false;
    }
    out = static_cast<int32_t>(value);
    return true;
}

int32_t pow10Int(uint8_t scale) {
    int32_t factor = 1;
    for (uint8_t i = 0; i < scale; ++i) {
        factor *= 10;
    }
    return factor;
}

bool formatDecimalRaw(int32_t raw, uint8_t scale, char* out, size_t len) {
    if (!out || len == 0 || scale > Esp32BaseAppConfig::DECIMAL_SCALE_MAX) {
        return false;
    }
    if (scale == 0) {
        snprintf(out, len, "%ld", static_cast<long>(raw));
        return true;
    }
    const int32_t factor = pow10Int(scale);
    int64_t absValue = raw;
    const bool neg = absValue < 0;
    if (neg) {
        absValue = -absValue;
    }
    char fracFmt[8];
    snprintf(fracFmt, sizeof(fracFmt), "%%0%ulld", static_cast<unsigned>(scale));
    char fracBuf[12];
    snprintf(fracBuf, sizeof(fracBuf), fracFmt, static_cast<long long>(absValue % factor));
    snprintf(out, len, "%s%lld.%s", neg ? "-" : "", static_cast<long long>(absValue / factor), fracBuf);
    return true;
}

bool parseDecimalRaw(const char* text, uint8_t scale, int32_t& rawOut) {
    if (!text || !text[0] || scale > Esp32BaseAppConfig::DECIMAL_SCALE_MAX) {
        return false;
    }
    const char* p = text;
    bool neg = false;
    if (*p == '-' || *p == '+') {
        neg = *p == '-';
        ++p;
    }
    const int32_t factor = pow10Int(scale);
    const int64_t maxAbsRaw = neg ? (static_cast<int64_t>(INT32_MAX) + 1LL) : static_cast<int64_t>(INT32_MAX);
    int64_t whole = 0;
    bool hadDigit = false;
    while (*p >= '0' && *p <= '9') {
        hadDigit = true;
        const int digit = *p - '0';
        whole = whole * 10 + digit;
        if (whole > maxAbsRaw / factor) {
            return false;
        }
        ++p;
    }
    int64_t frac = 0;
    uint8_t fracDigits = 0;
    if (*p == '.') {
        ++p;
        while (*p >= '0' && *p <= '9') {
            if (fracDigits >= scale) {
                return false;
            }
            frac = frac * 10 + (*p - '0');
            ++fracDigits;
            ++p;
        }
    }
    if (!hadDigit || *p != '\0') {
        return false;
    }
    while (fracDigits < scale) {
        frac *= 10;
        ++fracDigits;
    }
    int64_t raw = whole * factor + frac;
    if (raw > maxAbsRaw) {
        return false;
    }
    if (neg) {
        raw = -raw;
    }
    if (raw < INT32_MIN || raw > INT32_MAX) {
        return false;
    }
    rawOut = static_cast<int32_t>(raw);
    return true;
}

void appConfigFieldName(uint8_t index, char* out, size_t len) {
    snprintf(out, len, "f%u", static_cast<unsigned>(index));
}

bool getSubmittedRaw(const AppConfigFieldSlot& field, uint8_t index, char* out, size_t len) {
    if (!out || len == 0) {
        return false;
    }
    char name[8];
    appConfigFieldName(index, name, sizeof(name));
    if (field.type == APP_CFG_BOOL) {
        return strlcpy(out, g_server.hasArg(name) ? "1" : "0", len) < len;
    }
    if (!g_server.hasArg(name)) {
        out[0] = '\0';
        return false;
    }
    return strlcpy(out, g_server.arg(name).c_str(), len) < len;
}

bool findSubmittedRaw(const char* ns, const char* key, char* out, size_t len, const AppConfigFieldSlot** fieldOut = nullptr) {
    for (uint8_t i = 0; i < g_appConfigFieldCount; ++i) {
        const AppConfigFieldSlot& field = g_appConfigFields[i];
        if (strcmp(field.ns, ns) == 0 && strcmp(field.key, key) == 0) {
            if (fieldOut) {
                *fieldOut = &field;
            }
            return getSubmittedRaw(field, i, out, len);
        }
    }
    return false;
}

void readAppConfigValue(const AppConfigFieldSlot& field, char* textOut, size_t textLen, int32_t& rawOut, bool& boolOut) {
    textOut[0] = '\0';
    rawOut = 0;
    boolOut = false;
    switch (field.type) {
        case APP_CFG_STRING:
            Esp32BaseConfig::getStr(field.ns, field.key, textOut, textLen, field.spec.stringField.defaultValue ? field.spec.stringField.defaultValue : "");
            break;
        case APP_CFG_INT:
            rawOut = Esp32BaseConfig::getInt(field.ns, field.key, field.spec.intField.defaultValue);
            snprintf(textOut, textLen, "%ld", static_cast<long>(rawOut));
            break;
        case APP_CFG_DECIMAL:
            rawOut = Esp32BaseConfig::getInt(field.ns, field.key, field.spec.decimalField.defaultRawValue);
            formatDecimalRaw(rawOut, field.spec.decimalField.scale, textOut, textLen);
            break;
        case APP_CFG_BOOL:
            boolOut = Esp32BaseConfig::getBool(field.ns, field.key, field.spec.boolField.defaultValue);
            strlcpy(textOut, boolOut ? "true" : "false", textLen);
            break;
        case APP_CFG_ENUM:
            Esp32BaseConfig::getStr(field.ns, field.key, textOut, textLen, field.spec.enumField.defaultValue ? field.spec.enumField.defaultValue : "");
            break;
    }
}

const char* appConfigEnumLabel(const AppConfigFieldSlot& field, const char* value) {
    if (field.type == APP_CFG_ENUM && value) {
        for (uint8_t i = 0; i < field.spec.enumField.optionCount; ++i) {
            const Esp32BaseAppConfig::EnumOption& opt = field.spec.enumField.options[i];
            if (opt.value && strcmp(opt.value, value) == 0) {
                return opt.label ? opt.label : opt.value;
            }
        }
    }
    return value ? value : "";
}

void appConfigDisplayText(const AppConfigFieldSlot& field, const char* text, bool boolean, char* out, size_t len) {
    if (!out || len == 0) {
        return;
    }
    switch (field.type) {
        case APP_CFG_BOOL:
            strlcpy(out, boolean ? "Enabled" : "Disabled", len);
            break;
        case APP_CFG_ENUM:
            strlcpy(out, appConfigEnumLabel(field, text), len);
            break;
        default:
            strlcpy(out, text ? text : "", len);
            break;
    }
}

void clearAppConfigPendingRestart(AppConfigPendingRestartSlot& slot) {
    if (slot.oldText) {
        free(slot.oldText);
        slot.oldText = nullptr;
    }
    slot.active = false;
    slot.fieldIndex = 0;
    slot.oldRaw = 0;
    slot.oldBool = false;
    slot.oldTextUnavailable = false;
}

AppConfigPendingRestartSlot* findAppConfigPendingRestart(uint8_t fieldIndex) {
    for (uint8_t i = 0; i < ESP32BASE_APP_CONFIG_MAX_FIELDS; ++i) {
        if (g_appConfigPendingRestart[i].active && g_appConfigPendingRestart[i].fieldIndex == fieldIndex) {
            return &g_appConfigPendingRestart[i];
        }
    }
    return nullptr;
}

AppConfigPendingRestartSlot* allocAppConfigPendingRestart(uint8_t fieldIndex) {
    AppConfigPendingRestartSlot* existing = findAppConfigPendingRestart(fieldIndex);
    if (existing) {
        return existing;
    }
    for (uint8_t i = 0; i < ESP32BASE_APP_CONFIG_MAX_FIELDS; ++i) {
        if (!g_appConfigPendingRestart[i].active) {
            g_appConfigPendingRestart[i].active = true;
            g_appConfigPendingRestart[i].fieldIndex = fieldIndex;
            return &g_appConfigPendingRestart[i];
        }
    }
    return nullptr;
}

bool appConfigValueMatchesPendingOriginal(const AppConfigFieldSlot& field, const AppConfigPendingRestartSlot& slot,
                                          const char* newText, int32_t newRaw, bool newBool) {
    switch (field.type) {
        case APP_CFG_STRING:
        case APP_CFG_ENUM:
            if (slot.oldTextUnavailable) {
                return false;
            }
            return strcmp(slot.oldText ? slot.oldText : "", newText ? newText : "") == 0;
        case APP_CFG_INT:
        case APP_CFG_DECIMAL:
            return slot.oldRaw == newRaw;
        case APP_CFG_BOOL:
            return slot.oldBool == newBool;
    }
    return false;
}

void updateAppConfigPendingRestart(const AppConfigFieldSlot& field, uint8_t index,
                                   const char* oldText, int32_t oldRaw, bool oldBool,
                                   const char* newText, int32_t newRaw, bool newBool) {
    if (!field.restartRequired) {
        return;
    }
    AppConfigPendingRestartSlot* slot = findAppConfigPendingRestart(index);
    if (!slot) {
        slot = allocAppConfigPendingRestart(index);
        if (!slot) {
            ESP32BASE_LOG_W("appcfg", "pending_restart_full key=%s", field.key ? field.key : "-");
            return;
        }
        slot->oldRaw = oldRaw;
        slot->oldBool = oldBool;
        if (field.type == APP_CFG_STRING || field.type == APP_CFG_ENUM) {
            const char* value = oldText ? oldText : "";
            slot->oldText = static_cast<char*>(malloc(strlen(value) + 1));
            if (!slot->oldText) {
                slot->oldTextUnavailable = true;
                ESP32BASE_LOG_W("appcfg", "pending_restart_alloc_failed key=%s", field.key ? field.key : "-");
                return;
            }
            strcpy(slot->oldText, value);
        }
    }
    if (appConfigValueMatchesPendingOriginal(field, *slot, newText, newRaw, newBool)) {
        clearAppConfigPendingRestart(*slot);
    }
}

bool hasAppConfigPendingRestart() {
    for (uint8_t i = 0; i < ESP32BASE_APP_CONFIG_MAX_FIELDS; ++i) {
        if (g_appConfigPendingRestart[i].active) {
            return true;
        }
    }
    return false;
}

Esp32BaseAppConfig::FieldValue makeFieldValue(const AppConfigFieldSlot& field, const char* text, int32_t raw, bool boolean) {
    Esp32BaseAppConfig::FieldValue value = {publicFieldType(field.type), text, raw, boolean};
    return value;
}

bool validateSubmittedField(const AppConfigFieldSlot& field, const char* submitted, char* normalized, size_t normalizedLen,
                            int32_t& rawOut, bool& boolOut, char* error, size_t errorLen) {
    normalized[0] = '\0';
    rawOut = 0;
    boolOut = false;
    switch (field.type) {
        case APP_CFG_STRING: {
            const size_t len = submitted ? strlen(submitted) : 0;
            if (!submitted || len < field.spec.stringField.minLength || len > field.spec.stringField.maxLength) {
                snprintf(error, errorLen, "%s length must be %u..%u.", field.label, field.spec.stringField.minLength, field.spec.stringField.maxLength);
                return false;
            }
            strlcpy(normalized, submitted, normalizedLen);
            break;
        }
        case APP_CFG_INT:
            if (!parseStrictInt32(submitted, rawOut) || rawOut < field.spec.intField.minValue ||
                rawOut > field.spec.intField.maxValue || !stepMatches(rawOut, field.spec.intField.minValue, field.spec.intField.step)) {
                snprintf(error, errorLen, "%s is out of range.", field.label);
                return false;
            }
            snprintf(normalized, normalizedLen, "%ld", static_cast<long>(rawOut));
            break;
        case APP_CFG_DECIMAL:
            if (!parseDecimalRaw(submitted, field.spec.decimalField.scale, rawOut) ||
                rawOut < field.spec.decimalField.minRawValue || rawOut > field.spec.decimalField.maxRawValue ||
                !stepMatches(rawOut, field.spec.decimalField.minRawValue, field.spec.decimalField.stepRaw)) {
                snprintf(error, errorLen, "%s is out of range.", field.label);
                return false;
            }
            formatDecimalRaw(rawOut, field.spec.decimalField.scale, normalized, normalizedLen);
            break;
        case APP_CFG_BOOL:
            boolOut = submitted && strcmp(submitted, "1") == 0;
            strlcpy(normalized, boolOut ? "true" : "false", normalizedLen);
            break;
        case APP_CFG_ENUM:
            if (!submitted || !enumValueAllowed(field.spec.enumField, submitted)) {
                snprintf(error, errorLen, "%s has an invalid option.", field.label);
                return false;
            }
            strlcpy(normalized, submitted, normalizedLen);
            break;
    }
    if (field.validate) {
        Esp32BaseAppConfig::FieldRef ref = makeFieldRef(field);
        Esp32BaseAppConfig::FieldValue value = makeFieldValue(field, normalized, rawOut, boolOut);
        if (!field.validate(ref, value, error, errorLen)) {
            if (!error[0]) {
                snprintf(error, errorLen, "%s is invalid.", field.label);
            }
            return false;
        }
    }
    return true;
}

void sendAppConfigScript() {
    sendChunk("<script>"
              "function acVal(e){return e.type=='checkbox'?(e.checked?'true':'false'):e.value;}"
              "function acText(e){if(e.tagName=='SELECT')return e.options[e.selectedIndex]?e.options[e.selectedIndex].text:e.value;if(e.type=='checkbox')return e.checked?'Enabled':'Disabled';return e.value;}"
              "function acEsc(s){return String(s).replace(/[&<>\"']/g,function(c){return {'&':'&amp;','<':'&lt;','>':'&gt;','\"':'&quot;',\"'\":'&#39;'}[c];});}"
              "function acBuild(f){var rows='',n=0,g='',snap=[];f.querySelectorAll('[data-ac]').forEach(function(e){var a=e.dataset.initial||'',b=acVal(e),eg=e.dataset.group||'';snap.push([e.name,b]);if(a!=b){if(eg!=g){g=eg;rows+='<tr class=\"acgroup\"><th colspan=\"3\">'+acEsc(g)+'</th></tr>';}n++;rows+='<tr><td>'+acEsc(e.dataset.label)+'</td><td>'+acEsc(e.dataset.initialText||a)+'</td><td>'+acEsc(acText(e))+'</td></tr>';}});return {rows:rows,n:n,snap:JSON.stringify(snap)};}"
              "function acConfirm(f){var r=acBuild(f);if(!r.n){alert('No App Config changes.');return false;}var b=document.getElementById('acbox'),t=document.getElementById('acrows'),s=document.getElementById('acsavebar');if(!f.dataset.confirmed){t.innerHTML=r.rows;f.dataset.snapshot=r.snap;b.style.display='block';if(s)s.style.display='none';b.scrollIntoView({block:'nearest'});return false;}if(f.dataset.snapshot!=r.snap){delete f.dataset.confirmed;t.innerHTML=r.rows;f.dataset.snapshot=r.snap;b.style.display='block';alert('App Config changed. Review changes again.');return false;}return once(f);}"
              "function acCancel(f){f=f||document.querySelector('form');var b=document.getElementById('acbox'),s=document.getElementById('acsavebar');if(f){delete f.dataset.confirmed;delete f.dataset.snapshot;}b.style.display='none';if(s)s.style.display='block';}"
              "function acDirty(f){if(f.dataset.snapshot||f.dataset.confirmed)acCancel(f);}"
              "function acSubmit(f){f.dataset.confirmed='1';if(f.requestSubmit){f.requestSubmit();}else{var b=f.querySelector('[type=submit]');if(b)b.click();}}"
              "</script>");
}

void sendAppConfigTopMessage() {
    if (g_server.hasArg("saved")) {
        sendChunk("<p class='ok'><b>App Config saved.</b></p>");
    } else if (g_server.hasArg("partial")) {
        sendChunk("<p class='err'><b>Some App Config values were not saved.</b></p>");
    } else if (g_server.hasArg("restart")) {
        sendChunk("<p class='ok'><b>App Config saved.</b></p>");
    } else if (g_server.hasArg("stale")) {
        sendChunk("<p class='err'>Config page was stale. Reload and try again.</p>");
    }
}

void sendAppConfigPendingRestartNotice() {
    if (!hasAppConfigPendingRestart()) {
        return;
    }
    sendChunk("<section class='panel pendingbox'><h2>Restart required</h2><p class='muted'>Some saved App Config values take effect after restart.</p><table><thead><tr><th>Parameter</th><th>Running</th><th>Saved</th></tr></thead><tbody>");
    for (uint8_t i = 0; i < ESP32BASE_APP_CONFIG_MAX_FIELDS; ++i) {
        const AppConfigPendingRestartSlot& slot = g_appConfigPendingRestart[i];
        if (!slot.active || slot.fieldIndex >= g_appConfigFieldCount) {
            continue;
        }
        const AppConfigFieldSlot& field = g_appConfigFields[slot.fieldIndex];
        char oldText[Esp32BaseAppConfig::STRING_MAX_LENGTH + 1];
        char newText[Esp32BaseAppConfig::STRING_MAX_LENGTH + 1];
        char display[Esp32BaseAppConfig::STRING_MAX_LENGTH + 1];
        int32_t raw = 0;
        bool boolean = false;
        switch (field.type) {
            case APP_CFG_INT:
                snprintf(oldText, sizeof(oldText), "%ld", static_cast<long>(slot.oldRaw));
                break;
            case APP_CFG_DECIMAL:
                formatDecimalRaw(slot.oldRaw, field.spec.decimalField.scale, oldText, sizeof(oldText));
                break;
            case APP_CFG_BOOL:
                strlcpy(oldText, slot.oldBool ? "Enabled" : "Disabled", sizeof(oldText));
                break;
            case APP_CFG_STRING:
            case APP_CFG_ENUM:
                if (slot.oldTextUnavailable) {
                    strlcpy(oldText, "unavailable", sizeof(oldText));
                } else {
                    appConfigDisplayText(field, slot.oldText ? slot.oldText : "", false, oldText, sizeof(oldText));
                }
                break;
        }
        readAppConfigValue(field, newText, sizeof(newText), raw, boolean);
        appConfigDisplayText(field, newText, boolean, display, sizeof(display));
        sendChunk("<tr><td>");
        sendEscapedHtmlChunk(field.label);
        sendChunk("</td><td>");
        sendEscapedHtmlChunk(oldText);
        sendChunk("</td><td>");
        sendEscapedHtmlChunk(display);
        sendChunk("</td></tr>");
    }
    sendChunk("</tbody></table></section>");
}

void sendAppConfigPage(const char* errorMessage = nullptr) {
    markRequest();
    if (!ensureAuth()) {
        return;
    }
    g_pageHeadProgmem = WEB_APPCFG_STYLE;
    Esp32BaseWeb::sendHeader(g_appConfigTitle);
    sendAppConfigScript();
    sendChunk("<div class='appcfg'>");
    sendChunk("<header class='pagehead'><h1>");
    sendEscapedHtmlChunk(g_appConfigTitle);
    sendChunk("</h1></header>");
    sendAppConfigTopMessage();
    sendAppConfigPendingRestartNotice();
    if (errorMessage && errorMessage[0]) {
        sendChunk("<p class='err'>");
        sendEscapedHtmlChunk(errorMessage);
        sendChunk("</p>");
    }
    if (g_appConfigGroupCount == 0 || g_appConfigFieldCount == 0) {
        sendChunk("<section class='panel'><p class='muted'>No App Config fields registered.</p></section>");
        sendChunk("</div>");
        Esp32BaseWeb::sendFooter();
        return;
    }
    char buf[32];
    sendChunk("<form method='post' action='/esp32base/app-config' onsubmit='return acConfirm(this)' oninput='acDirty(this)' onchange='acDirty(this)'><input type='hidden' name='rev' value='");
    snprintf(buf, sizeof(buf), "%lu", static_cast<unsigned long>(g_appConfigRevision));
    sendEscapedHtmlChunk(buf);
    sendChunk("'>");
    for (uint8_t g = 0; g < g_appConfigGroupCount; ++g) {
        sendChunk("<section class='panel'><h2>");
        sendEscapedHtmlChunk(g_appConfigGroups[g].title);
        sendChunk("</h2>");
        for (uint8_t i = 0; i < g_appConfigFieldCount; ++i) {
            const AppConfigFieldSlot& field = g_appConfigFields[i];
            if (strcmp(field.groupId, g_appConfigGroups[g].id) != 0) {
                continue;
            }
            char value[Esp32BaseAppConfig::STRING_MAX_LENGTH + 1];
            int32_t raw = 0;
            bool boolean = false;
            readAppConfigValue(field, value, sizeof(value), raw, boolean);
            char name[8];
            appConfigFieldName(i, name, sizeof(name));
            sendChunk("<div class='acfield'><label for='");
            sendEscapedHtmlChunk(name);
            sendChunk("'>");
            sendEscapedHtmlChunk(field.label);
            if (field.restartRequired) {
                sendChunk(" <span class='restart'>(restart)</span>");
            }
            sendChunk("</label>");
            if (field.type == APP_CFG_BOOL) {
                sendChunk("<label><input id='");
                sendEscapedHtmlChunk(name);
                sendChunk("' type='checkbox' name='");
                sendEscapedHtmlChunk(name);
                sendChunk("' value='1' data-ac='1' data-label='");
                sendEscapedHtmlChunk(field.label);
                sendChunk("' data-group='");
                sendEscapedHtmlChunk(g_appConfigGroups[g].title);
                sendChunk("' data-initial='");
                sendEscapedHtmlChunk(boolean ? "true" : "false");
                sendChunk("' data-initial-text='");
                sendEscapedHtmlChunk(boolean ? "Enabled" : "Disabled");
                sendChunk("'");
                if (boolean) {
                    sendChunk(" checked");
                }
                sendChunk("> Enabled</label>");
            } else if (field.type == APP_CFG_ENUM) {
                sendChunk("<select id='");
                sendEscapedHtmlChunk(name);
                sendChunk("' name='");
                sendEscapedHtmlChunk(name);
                sendChunk("' data-ac='1' data-label='");
                sendEscapedHtmlChunk(field.label);
                sendChunk("' data-group='");
                sendEscapedHtmlChunk(g_appConfigGroups[g].title);
                sendChunk("' data-initial='");
                sendEscapedHtmlChunk(value);
                sendChunk("' data-initial-text='");
                for (uint8_t o = 0; o < field.spec.enumField.optionCount; ++o) {
                    const Esp32BaseAppConfig::EnumOption& opt = field.spec.enumField.options[o];
                    if (strcmp(value, opt.value) == 0) {
                        sendEscapedHtmlChunk(opt.label);
                        break;
                    }
                }
                sendChunk("'>");
                for (uint8_t o = 0; o < field.spec.enumField.optionCount; ++o) {
                    const Esp32BaseAppConfig::EnumOption& opt = field.spec.enumField.options[o];
                    sendChunk("<option value='");
                    sendEscapedHtmlChunk(opt.value);
                    sendChunk("'");
                    if (strcmp(value, opt.value) == 0) {
                        sendChunk(" selected");
                    }
                    sendChunk(">");
                    sendEscapedHtmlChunk(opt.label);
                    sendChunk("</option>");
                }
                sendChunk("</select>");
            } else {
                sendChunk("<div class='acrow'><input id='");
                sendEscapedHtmlChunk(name);
                sendChunk("' name='");
                sendEscapedHtmlChunk(name);
                sendChunk("' data-ac='1' data-label='");
                sendEscapedHtmlChunk(field.label);
                sendChunk("' data-group='");
                sendEscapedHtmlChunk(g_appConfigGroups[g].title);
                sendChunk("' data-initial='");
                sendEscapedHtmlChunk(value);
                sendChunk("' value='");
                sendEscapedHtmlChunk(value);
                sendChunk("'");
                if (field.type == APP_CFG_STRING) {
                    snprintf(buf, sizeof(buf), "%u", field.spec.stringField.maxLength);
                    sendChunk(" maxlength='");
                    sendEscapedHtmlChunk(buf);
                    sendChunk("'");
                } else {
                    sendChunk(" type='number'");
                    if (field.type == APP_CFG_INT) {
                        sendChunk(" min='");
                        snprintf(buf, sizeof(buf), "%ld", static_cast<long>(field.spec.intField.minValue));
                        sendEscapedHtmlChunk(buf);
                        sendChunk("' max='");
                        snprintf(buf, sizeof(buf), "%ld", static_cast<long>(field.spec.intField.maxValue));
                        sendEscapedHtmlChunk(buf);
                        sendChunk("' step='");
                        snprintf(buf, sizeof(buf), "%ld", static_cast<long>(field.spec.intField.step));
                        sendEscapedHtmlChunk(buf);
                        sendChunk("'");
                    } else if (field.type == APP_CFG_DECIMAL) {
                        char num[32];
                        sendChunk(" min='");
                        formatDecimalRaw(field.spec.decimalField.minRawValue, field.spec.decimalField.scale, num, sizeof(num));
                        sendEscapedHtmlChunk(num);
                        sendChunk("' max='");
                        formatDecimalRaw(field.spec.decimalField.maxRawValue, field.spec.decimalField.scale, num, sizeof(num));
                        sendEscapedHtmlChunk(num);
                        sendChunk("' step='");
                        formatDecimalRaw(field.spec.decimalField.stepRaw, field.spec.decimalField.scale, num, sizeof(num));
                        sendEscapedHtmlChunk(num);
                        sendChunk("'");
                    }
                }
                sendChunk(">");
                if (field.unit && field.unit[0]) {
                    sendChunk("<span class='unit'>");
                    sendEscapedHtmlChunk(field.unit);
                    sendChunk("</span>");
                }
                sendChunk("</div>");
            }
            if (field.help && field.help[0]) {
                sendChunk("<p class='help'>");
                sendEscapedHtmlChunk(field.help);
                sendChunk("</p>");
            }
            sendChunk("</div>");
        }
        sendChunk("</section>");
    }
    sendChunk("<div id='acbox' class='confirmbox'><h2>Confirm changes</h2><table><thead><tr><th>Parameter</th><th>Old</th><th>New</th></tr></thead><tbody id='acrows'></tbody></table><div class='actions'><input type='button' value='Confirm Save' onclick='acSubmit(this.form)'> <input type='button' value='Cancel' onclick='acCancel(this.form)'></div></div><div id='acsavebar' class='actions'><input type='submit' value='Save App Config'></div></form></div>");
    Esp32BaseWeb::sendFooter();
}

bool validateAllSubmitted(char* error, size_t errorLen) {
    char submitted[Esp32BaseAppConfig::STRING_MAX_LENGTH + 1];
    char normalized[Esp32BaseAppConfig::STRING_MAX_LENGTH + 1];
    for (uint8_t i = 0; i < g_appConfigFieldCount; ++i) {
        const AppConfigFieldSlot& field = g_appConfigFields[i];
        if (!getSubmittedRaw(field, i, submitted, sizeof(submitted))) {
            snprintf(error, errorLen, "%s is missing.", field.label);
            return false;
        }
        int32_t raw = 0;
        bool boolean = false;
        if (!validateSubmittedField(field, submitted, normalized, sizeof(normalized), raw, boolean, error, errorLen)) {
            return false;
        }
    }
    if (g_appConfigPageValidate) {
        g_appConfigSubmitContext = true;
        const bool ok = g_appConfigPageValidate(error, errorLen);
        g_appConfigSubmitContext = false;
        if (!ok) {
            if (!error[0]) {
                strlcpy(error, "App Config is invalid.", errorLen);
            }
            return false;
        }
    }
    return true;
}

bool writeSubmittedField(const AppConfigFieldSlot& field, uint8_t index, Esp32BaseAppConfig::SaveSummary& summary) {
    char submitted[Esp32BaseAppConfig::STRING_MAX_LENGTH + 1];
    char normalized[Esp32BaseAppConfig::STRING_MAX_LENGTH + 1];
    char oldText[Esp32BaseAppConfig::STRING_MAX_LENGTH + 1];
    char error[128] = "";
    int32_t newRaw = 0;
    int32_t oldRaw = 0;
    bool newBool = false;
    bool oldBool = false;
    getSubmittedRaw(field, index, submitted, sizeof(submitted));
    validateSubmittedField(field, submitted, normalized, sizeof(normalized), newRaw, newBool, error, sizeof(error));
    readAppConfigValue(field, oldText, sizeof(oldText), oldRaw, oldBool);
    bool changed = false;
    bool ok = true;
    switch (field.type) {
        case APP_CFG_STRING:
        case APP_CFG_ENUM:
            changed = strcmp(oldText, normalized) != 0;
            if (changed) {
                ok = Esp32BaseConfig::setStr(field.ns, field.key, normalized);
            }
            break;
        case APP_CFG_INT:
        case APP_CFG_DECIMAL:
            changed = oldRaw != newRaw;
            if (changed) {
                ok = Esp32BaseConfig::setInt(field.ns, field.key, newRaw);
            }
            break;
        case APP_CFG_BOOL:
            changed = oldBool != newBool;
            if (changed) {
                ok = Esp32BaseConfig::setBool(field.ns, field.key, newBool);
            }
            break;
    }
    if (!changed) {
        return true;
    }
    summary.changedCount++;
    if (!ok) {
        summary.failedCount++;
        return false;
    }
    summary.savedCount++;
    summary.restartRequired = summary.restartRequired || field.restartRequired;
    updateAppConfigPendingRestart(field, index, oldText, oldRaw, oldBool, normalized, newRaw, newBool);
    if (g_appConfigChangeCallback) {
        Esp32BaseAppConfig::Change change;
        change.field = makeFieldRef(field);
        change.oldValue = makeFieldValue(field, oldText, oldRaw, oldBool);
        change.newValue = makeFieldValue(field, normalized, newRaw, newBool);
        g_appConfigChangeCallback(change);
    }
    return true;
}

void handleAppConfigSubmit() {
    markRequest();
    if (!ensurePostAllowed("app_config")) {
        return;
    }
    int32_t rev = 0;
    if (!g_server.hasArg("rev") || !parseStrictInt32(g_server.arg("rev").c_str(), rev) || static_cast<uint32_t>(rev) != g_appConfigRevision) {
        redirectSeeOther("/esp32base/app-config?stale=1");
        return;
    }
    char error[128] = "";
    if (!validateAllSubmitted(error, sizeof(error))) {
        sendAppConfigPage(error);
        return;
    }
    Esp32BaseAppConfig::SaveSummary summary = {};
    for (uint8_t i = 0; i < g_appConfigFieldCount; ++i) {
        writeSubmittedField(g_appConfigFields[i], i, summary);
    }
    if (summary.savedCount > 0) {
        g_appConfigRevision++;
    }
    if (g_appConfigSaveCallback) {
        g_appConfigSaveCallback(summary);
    }
    if (summary.failedCount > 0) {
        redirectSeeOther("/esp32base/app-config?partial=1");
    } else if (summary.restartRequired) {
        redirectSeeOther("/esp32base/app-config?restart=1");
    } else {
        redirectSeeOther(summary.savedCount > 0 ? "/esp32base/app-config?saved=1" : "/esp32base/app-config");
    }
}

void handleAppConfigPage() {
    sendAppConfigPage();
}
#endif

void handleRestart() {
    markRequest();
    if (!ensurePostAllowed("restart")) {
        return;
    }
    g_server.send(200, "text/plain", "restarting");
    delay(100);
    Esp32BaseSystem::restart("web");
}

void handleToolsPage() {
    markRequest();
    if (!ensureAuth()) {
        return;
    }
    Esp32BaseWeb::sendHeader(g_builtinLabels[Esp32BaseWeb::BUILTIN_TOOLS]);
    sendChunk("<header class='pagehead'><h1>");
    sendEscapedHtmlChunk(g_builtinLabels[Esp32BaseWeb::BUILTIN_TOOLS]);
    sendChunk("</h1></header>");
    sendChunk("<section class='panel'><h2>System settings</h2><div class='quicklinks'>");
#if ESP32BASE_ENABLE_APP_CONFIG
    sendNavLink("/esp32base/app-config", "App Config", false, nullptr);
#endif
    sendNavLink("/esp32base/wifi", "WiFi Setup", false, nullptr);
    sendNavLink("/esp32base/auth", "Web Auth", false, nullptr);
#if ESP32BASE_ENABLE_OTA
    sendNavLink("/esp32base/ota", "Firmware OTA", false, nullptr);
#endif
    sendChunk("</div><p class='muted'>Low-frequency device settings.</p></section>");
    if (g_server.hasArg("formatted")) {
        sendChunk("<p class='ok'><b>LittleFS formatted.</b> FileLog mode was reloaded.</p>");
    } else if (g_server.hasArg("logs_cleared")) {
        sendChunk("<p class='ok'><b>Logs cleared.</b></p>");
    } else if (g_server.hasArg("filelog_saved")) {
        sendChunk("<p class='ok'><b>File log mode saved.</b></p>");
    } else if (g_server.hasArg("watchdog_trip_reset")) {
        sendChunk("<p class='ok'><b>Watchdog trip reset.</b></p>");
    } else if (g_server.hasArg("error")) {
        sendChunk("<p class='err'>System action failed: ");
        sendEscapedHtmlChunk(g_server.arg("error").c_str());
        sendChunk("</p>");
    } else if (g_server.hasArg("restarting")) {
        sendChunk("<p class='ok'><b>Restart requested.</b> Device is restarting. Please wait a few seconds, then reload.</p>");
    }
    if (g_server.hasArg("hostname_saved")) {
        sendChunk("<p class='ok'><b>Hostname saved.</b> Restart the device for mDNS, OTA and Web discovery to use it.</p>");
    } else if (g_server.hasArg("hostname_error")) {
        sendChunk("<p class='err'>Hostname was not saved: ");
        sendEscapedHtmlChunk(g_server.arg("hostname_error").c_str());
        sendChunk("</p>");
    }
    char storedHostname[64] = "";
    const bool hasStoredHostname = loadStoredHostname(storedHostname, sizeof(storedHostname));
    sendChunk("<section class='panel'><h2>Hostname</h2><table class='kv'>");
    sendInfoRow("Current", Esp32Base::hostname());
    sendInfoRow("Default", Esp32Base::defaultHostname());
    sendInfoRow("Stored", hasStoredHostname && storedHostname[0] ? storedHostname : "-");
    sendInfoRow("Restart required", hostnameRestartRequired(storedHostname) ? "yes" : "no");
    sendChunk("</table><form method='post' action='/esp32base/tools/hostname' onsubmit=\"var h=this.hostname.value.trim();if(!/^[a-z0-9](?:[a-z0-9-]{0,30}[a-z0-9])?$/.test(h)){alert('Use 1-32 lowercase letters, digits and hyphen. No leading or trailing hyphen. Do not include .local.');return false;}this.hostname.value=h;return once(this);\">");
    sendChunk("<label for='host'>New hostname</label><input id='host' name='hostname' maxlength='32' autocomplete='off' value='");
    sendEscapedHtmlChunk(hasStoredHostname && storedHostname[0] ? storedHostname : Esp32Base::hostname());
    sendChunk("'><p class='muted'>Saved hostname takes effect after restart.</p><input type='submit' value='Save Hostname'></form></section>");
#if ESP32BASE_ENABLE_WATCHDOG
    sendWatchdogPanel();
#endif
#if ESP32BASE_ENABLE_FILELOG
    sendChunk("<section class='panel'><h2>File log</h2><table class='kv'>");
    sendInfoRow("Current mode", Esp32BaseFileLog::modeName());
    sendInfoRow("Path", Esp32BaseFileLog::path());
    sendChunk("</table><form method='post' action='/esp32base/tools/filelog' onsubmit=\"return once(this)\"><div class='quicklinks'>");
    sendFileLogModeOption("off", "Off", Esp32BaseFileLog::OFF);
#if ESP32BASE_LOG_LEVEL >= ESP32BASE_LOG_WARN
    sendFileLogModeOption("warn", "WARN", Esp32BaseFileLog::WARN);
#endif
#if ESP32BASE_LOG_LEVEL >= ESP32BASE_LOG_INFO
    sendFileLogModeOption("info", "INFO", Esp32BaseFileLog::INFO);
#endif
    sendChunk("</div><p class='muted'>File log mode is capped by the build log level.</p><input type='submit' value='Save File Log'></form></section>");
#else
    sendChunk("<section class='panel'><h2>File log</h2><p class='muted'>File log is unavailable in this profile.</p></section>");
#endif
    sendChunk("<section class='panel'><h2>Restart device</h2><p class='muted'>Restart the device through the normal lifecycle path.</p><form method='post' action='/esp32base/tools/reboot' onsubmit=\"return confirm('Reboot device now?')&&once(this)\"><input class='danger' type='submit' value='Restart device'></form></section>");
#if ESP32BASE_ENABLE_FS
    sendChunk("<section class='panel'><h2>Format LittleFS</h2><p class='dangertext'>This deletes logs and all files stored in LittleFS. WiFi, Web Auth and NVS config are not cleared.</p><form method='post' action='/esp32base/tools/format-fs' onsubmit=\"return confirm('Format LittleFS? This deletes logs and all files stored in LittleFS.')&&once(this)\"><input class='danger' type='submit' value='Format LittleFS'></form></section>");
#else
    sendChunk("<section class='panel'><h2>Format LittleFS</h2><p class='muted'>LittleFS is unavailable in this profile.</p></section>");
#endif
#if ESP32BASE_ENABLE_FILELOG
    sendChunk("<section class='panel'><h2>Clear logs</h2><p class='dangertext'>Delete all file log contents. Runtime settings and WiFi credentials are not changed.</p><form method='post' action='/esp32base/tools/logs-clear' onsubmit=\"return confirm('Clear log files?')&&once(this)\"><input class='danger' type='submit' value='Clear Logs'></form></section>");
#else
    sendChunk("<section class='panel'><h2>Clear logs</h2><p class='muted'>File log is unavailable in this profile.</p></section>");
#endif
    Esp32BaseWeb::sendFooter();
}

void handleToolsFileLogPost() {
    markRequest();
    if (!ensurePostAllowed("tools_filelog")) {
        return;
    }
#if ESP32BASE_ENABLE_FILELOG
    Esp32BaseFileLog::Mode mode = Esp32BaseFileLog::OFF;
    if (!fileLogModeFromArg(g_server.arg("mode"), mode)) {
        ESP32BASE_LOG_W("web", "filelog_mode_rejected source=tools value=%s", g_server.arg("mode").c_str());
        redirectSeeOther("/esp32base/tools?error=filelog_invalid_mode");
        return;
    }
    ESP32BASE_LOG_W("web", "filelog_mode_requested source=tools from=%s to=%s",
                    Esp32BaseFileLog::modeName(),
                    mode == Esp32BaseFileLog::OFF ? "OFF" : (mode == Esp32BaseFileLog::WARN ? "WARN" : "INFO"));
    const bool ok = Esp32BaseFileLog::setMode(mode);
    redirectSeeOther(ok ? "/esp32base/tools?filelog_saved=1" : "/esp32base/tools?error=filelog_save_failed");
#else
    redirectSeeOther("/esp32base/tools?error=filelog_unavailable");
#endif
}

void handleToolsRebootPost() {
    markRequest();
    if (!ensurePostAllowed("tools_reboot")) {
        return;
    }
    ESP32BASE_LOG_W("web", "restart_requested source=tools");
    Esp32BaseWeb::sendHeader("Rebooting");
    sendChunk("<script>history.replaceState(null,'','/esp32base/tools?restarting=1');</script><header class='pagehead'><h1>Rebooting...</h1></header><section class='panel'><p>Device is restarting. Please wait a few seconds, then reload.</p></section>");
    Esp32BaseWeb::sendFooter();
    delay(100);
    Esp32BaseSystem::restart("web");
}

#if ESP32BASE_ENABLE_WATCHDOG
void handleToolsWatchdogTripResetPost() {
    markRequest();
    if (!ensurePostAllowed("tools_watchdog_trip_reset")) {
        return;
    }
    const uint32_t lifetime = Esp32BaseWatchdog::lifetimeResetCount();
    const uint32_t resetTime = currentWatchdogTripResetTime();
    const bool writeOk = Esp32BaseConfig::setInt("eb_sys", "wdt_trip_base", static_cast<int32_t>(lifetime)) &&
                         Esp32BaseConfig::setInt("eb_sys", "wdt_trip_time", static_cast<int32_t>(resetTime));
    const bool verifyOk = Esp32BaseConfig::getInt("eb_sys", "wdt_trip_base", -1) == static_cast<int32_t>(lifetime) &&
                          Esp32BaseConfig::getInt("eb_sys", "wdt_trip_time", -1) == static_cast<int32_t>(resetTime);
    const bool ok = writeOk && verifyOk;
    ESP32BASE_LOG_W("web", "watchdog_trip_reset_requested source=tools lifetime=%lu time=%lu result=%s",
                    static_cast<unsigned long>(lifetime),
                    static_cast<unsigned long>(resetTime),
                    ok ? "success" : "failed");
    redirectSeeOther(ok ? "/esp32base/tools?watchdog_trip_reset=1" : "/esp32base/tools?error=watchdog_trip_reset_failed");
}
#endif

void handleToolsFormatFsPost() {
    markRequest();
    if (!ensurePostAllowed("tools_format_fs")) {
        return;
    }
#if ESP32BASE_ENABLE_FS
    ESP32BASE_LOG_W("web", "fs_format_requested source=tools");
#if ESP32BASE_ENABLE_FILELOG
    Esp32BaseFileLog::flush();
#endif
    const bool formatted = Esp32BaseFs::format();
    const bool mounted = formatted && Esp32BaseFs::begin();
    bool fileLogReloaded = false;
#if ESP32BASE_ENABLE_FILELOG
    if (mounted) {
        fileLogReloaded = Esp32BaseFileLog::begin();
    }
#endif
    ESP32BASE_LOG_W("web", "fs_format_completed source=tools format=%s mount=%s filelog_reload=%s",
                    formatted ? "success" : "failed",
                    mounted ? "success" : "failed",
                    fileLogReloaded ? "success" : "skipped");
    if (formatted && mounted) {
        redirectSeeOther("/esp32base/tools?formatted=1");
    } else {
        redirectSeeOther(formatted ? "/esp32base/tools?error=mount_failed" : "/esp32base/tools?error=format_failed");
    }
#else
    ESP32BASE_LOG_W("web", "fs_format_requested source=tools result=unavailable");
    redirectSeeOther("/esp32base/tools?error=fs_unavailable");
#endif
}

void handleToolsLogsClearPost() {
    markRequest();
    if (!ensurePostAllowed("tools_logs_clear")) {
        return;
    }
#if ESP32BASE_ENABLE_FILELOG
    const bool ok = Esp32BaseFileLog::clear();
    redirectSeeOther(ok ? "/esp32base/tools?logs_cleared=1" : "/esp32base/tools?error=logs_clear_failed");
#else
    redirectSeeOther("/esp32base/tools?error=logs_unavailable");
#endif
}

void handleCaptiveProbe() {
    g_server.sendHeader("Location", "/esp32base/wifi", true);
    g_server.send(302, "text/plain", "");
}

void handleRootRedirect() {
    const char* location = g_homeMode == Esp32BaseWeb::HOME_ESP32BASE ? "/esp32base" : configuredHomePath();
    g_server.sendHeader("Location", location, true);
    g_server.send(302, "text/plain", "");
}

void handleNoContent() {
    g_server.send(204, "text/plain", "");
}

void handleNotFound() {
    markRequest();
    const String uri = g_server.uri();
    if (uri.length() > 1 && uri.endsWith("/")) {
        String normalized = uri;
        while (normalized.length() > 1 && normalized.endsWith("/")) {
            normalized.remove(normalized.length() - 1);
        }
        g_server.sendHeader("Location", normalized, true);
        g_server.sendHeader("Cache-Control", "no-store");
        g_server.send(g_server.method() == HTTP_GET ? 302 : 307, "text/plain", "");
        return;
    }
    g_server.send(404, "text/plain", "Not found");
}

#if ESP32BASE_ENABLE_OTA
void handleOtaPage() {
    markRequest();
    if (!ensureAuth()) {
        return;
    }
    Esp32BaseWeb::sendHeader(g_builtinLabels[Esp32BaseWeb::BUILTIN_OTA]);
    sendChunk("<header class='pagehead'><h1>OTA Update</h1></header><section class='panel'><form id='f'><label for='fw'>Firmware file</label><input id='fw' type='file' name='firmware' accept='.bin' required><label for='sha'>SHA256</label><input id='sha' placeholder='Optional 64-character digest' maxlength='64'><input type='submit' value='Upload Firmware'></form><progress id='p' value='0' max='100' style='width:100%;display:none'></progress><p id='s' class='statusline muted'></p></section>");
    sendChunk("<script>function h(n){var u=['B','KB','MB','GB'],i=0,x=n;while(x>=1024&&i<u.length-1){x/=1024;i++;}return n+' bytes ('+(i?x.toFixed(2):x)+' '+u[i]+')';}document.getElementById('f').onsubmit=function(e){e.preventDefault();if(this.dataset.busy)return false;this.dataset.busy=1;var f=document.getElementById('fw').files[0],st=document.getElementById('s'),pg=document.getElementById('p'),b=this.querySelector('[type=submit]');if(!f){this.dataset.busy='';return false;}var d=new FormData();d.append('firmware',f,f.name);var x=new XMLHttpRequest();if(b)b.disabled=true;pg.style.display='block';st.textContent='Uploading 0%';x.upload.onprogress=function(ev){if(ev.lengthComputable){var p=Math.floor(ev.loaded*100/ev.total);pg.value=p;st.textContent='Uploading '+p+'% '+h(ev.loaded)+' / '+h(ev.total);}};x.onload=function(){var r={};try{r=JSON.parse(x.responseText||'{}');}catch(e){}if(x.status==200){st.textContent='Upload successful. Restarting...';setTimeout(function(){location.href=r.redirect||'/esp32base';},8000);return;}st.textContent='Upload failed: '+(r.error||('HTTP '+x.status));document.getElementById('f').dataset.busy='';if(b)b.disabled=false;};x.onerror=function(){st.textContent='Upload failed: network error';document.getElementById('f').dataset.busy='';if(b)b.disabled=false;};x.open('POST','/esp32base/ota');x.setRequestHeader('X-Firmware-Size',String(f.size));var s=document.getElementById('sha').value;if(s)x.setRequestHeader('X-Sha256',s);x.send(d);return false;};</script>");
    Esp32BaseWeb::sendFooter();
}

void handleOtaUploadDone() {
    markRequest();
    if (!ensureAuth()) {
        return;
    }
    if (g_otaUploadForbidden || !requestSameOrigin()) {
        ESP32BASE_LOG_W("web", "post_rejected context=ota_upload reason=cross_origin");
        g_otaUploadForbidden = false;
        g_otaUploadStartFailed = false;
        g_server.send(403, "application/json", "{\"ok\":false,\"error\":\"forbidden\"}");
        return;
    }
    g_otaUploadForbidden = false;
    const bool startFailed = g_otaUploadStartFailed;
    g_otaUploadStartFailed = false;
    const bool ok = Esp32BaseOta::status() == Esp32BaseOta::SUCCESS;
    if (ok) {
        if (beginResponse(200, "application/json", nullptr)) {
            sendChunk("{\"ok\":true,\"redirect\":\"");
            sendEscapedJsonChunk(configuredHomePath());
            sendChunk("\",\"rssi\":");
            sendIntChunk(Esp32BaseWiFi::isConnected() ? Esp32BaseWiFi::rssi() : 0);
            sendChunk(",\"elapsedMs\":");
            sendIntChunk(Esp32BaseOta::elapsedMs());
            sendChunk(",\"averageBytesPerSecond\":");
            sendIntChunk(Esp32BaseOta::averageBytesPerSecond());
            sendChunk("}");
            endResponse();
        }
    } else {
        if (beginResponse(500, "application/json", nullptr)) {
            sendChunk("{\"ok\":false,\"error\":\"");
            sendEscapedJsonChunk(startFailed && !Esp32BaseOta::lastError()[0] ? "ota upload rejected" : Esp32BaseOta::lastError());
            sendChunk("\"}");
            endResponse();
        }
    }
    if (ok) {
        delay(500);
        Esp32BaseSystem::restart("ota success");
    }
}

void handleOtaUpload() {
    if (!isAuthenticated()) {
        if (g_server.upload().status == UPLOAD_FILE_START) {
            Esp32BaseOta::abortUpload("unauthorized ota upload");
        }
        return;
    }
    HTTPUpload& upload = g_server.upload();
    if (upload.status == UPLOAD_FILE_START) {
        g_otaUploadForbidden = false;
        g_otaUploadStartFailed = false;
        if (!requestSameOrigin()) {
            g_otaUploadForbidden = true;
            ESP32BASE_LOG_W("web", "post_rejected context=ota_upload reason=cross_origin");
            Esp32BaseOta::abortUpload("forbidden ota upload origin");
            return;
        }
        char sha256[65] = "";
        size_t firmwareSize = upload.totalSize;
        if (g_server.hasHeader("X-Sha256")) {
            strlcpy(sha256, g_server.header("X-Sha256").c_str(), sizeof(sha256));
        } else if (g_server.hasArg("sha256")) {
            strlcpy(sha256, g_server.arg("sha256").c_str(), sizeof(sha256));
        }
        if (firmwareSize == 0 && g_server.hasHeader("X-Firmware-Size")) {
            firmwareSize = static_cast<size_t>(strtoul(g_server.header("X-Firmware-Size").c_str(), nullptr, 10));
        }
        if (!Esp32BaseOta::startUpload(firmwareSize, sha256[0] ? sha256 : nullptr)) {
            g_otaUploadStartFailed = true;
            ESP32BASE_LOG_W("web", "ota_upload_start_rejected error=%s", Esp32BaseOta::lastError());
            return;
        }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (g_otaUploadForbidden || g_otaUploadStartFailed) {
            return;
        }
        Esp32BaseOta::writeChunk(upload.buf, upload.currentSize);
    } else if (upload.status == UPLOAD_FILE_END) {
        if (g_otaUploadForbidden || g_otaUploadStartFailed) {
            return;
        }
        Esp32BaseOta::finishUpload();
    } else if (upload.status == UPLOAD_FILE_ABORTED) {
        Esp32BaseOta::abortUpload("client aborted");
    }
}

void handleOtaRawUpload() {
    if (!isAuthenticated()) {
        if (g_server.raw().status == RAW_START) {
            Esp32BaseOta::abortUpload("unauthorized ota raw upload");
        }
        return;
    }
    HTTPRaw& raw = g_server.raw();
    if (raw.status == RAW_START) {
        g_otaUploadForbidden = false;
        g_otaUploadStartFailed = false;
        if (!requestSameOrigin()) {
            g_otaUploadForbidden = true;
            ESP32BASE_LOG_W("web", "post_rejected context=ota_raw_upload reason=cross_origin");
            Esp32BaseOta::abortUpload("forbidden ota raw upload origin");
            return;
        }
        char sha256[65] = "";
        size_t firmwareSize = 0;
        if (g_server.hasHeader("X-Sha256")) {
            strlcpy(sha256, g_server.header("X-Sha256").c_str(), sizeof(sha256));
        }
        if (g_server.hasHeader("X-Firmware-Size")) {
            firmwareSize = static_cast<size_t>(strtoul(g_server.header("X-Firmware-Size").c_str(), nullptr, 10));
        }
        if (!Esp32BaseOta::startUpload(firmwareSize, sha256[0] ? sha256 : nullptr)) {
            g_otaUploadStartFailed = true;
            ESP32BASE_LOG_W("web", "ota_raw_upload_start_rejected error=%s", Esp32BaseOta::lastError());
            return;
        }
    } else if (raw.status == RAW_WRITE) {
        if (g_otaUploadForbidden || g_otaUploadStartFailed) {
            return;
        }
        Esp32BaseOta::writeChunk(raw.buf, raw.currentSize);
    } else if (raw.status == RAW_END) {
        if (g_otaUploadForbidden || g_otaUploadStartFailed) {
            return;
        }
        Esp32BaseOta::finishUpload();
    } else if (raw.status == RAW_ABORTED) {
        Esp32BaseOta::abortUpload("client aborted raw upload");
    }
}

void handleOtaApi() {
    markRequest();
    if (!ensureAuth()) {
        return;
    }
    if (!beginResponse(200, "application/json", nullptr)) {
        return;
    }
    sendChunk("{\"status\":");
    sendIntChunk(static_cast<int>(Esp32BaseOta::status()));
    sendChunk(",\"progress\":");
    sendIntChunk(Esp32BaseOta::progress());
    sendChunk(",\"bytesProcessed\":");
    sendBytesJsonChunk(static_cast<uint32_t>(Esp32BaseOta::bytesProcessed()));
    sendChunk(",\"totalSize\":");
    sendBytesJsonChunk(static_cast<uint32_t>(Esp32BaseOta::totalSize()));
    sendChunk(",\"elapsedMs\":");
    sendIntChunk(Esp32BaseOta::elapsedMs());
    sendChunk(",\"averageBytesPerSecond\":");
    sendIntChunk(Esp32BaseOta::averageBytesPerSecond());
    sendChunk(",\"error\":\"");
    sendEscapedJsonChunk(Esp32BaseOta::lastError());
    sendChunk("\"}");
    endResponse();
}
#endif

void handleRoot() {
    markRequest();
    if (!ensureAuth()) {
        return;
    }
    if (useAppHome()) {
        redirectSeeOther(configuredHomePath());
        return;
    }
    Esp32BaseWeb::sendHeader(g_builtinLabels[Esp32BaseWeb::BUILTIN_HOME]);
    sendChunk("<header class='pagehead'><h1>");
    sendEscapedHtmlChunk(g_deviceName);
    sendChunk("</h1></header>");

    char value[128];
    sendStatusSectionStart("Overview");
    sendInfoRowStart("Firmware");
    sendEscapedHtmlChunk(Esp32Base::firmwareName());
    sendChunk(" ");
    sendEscapedHtmlChunk(Esp32Base::firmwareVersion());
    if (Esp32Base::firmwareBuild()[0]) {
        sendChunk(" <span class='info'>");
        sendEscapedHtmlChunk(Esp32Base::firmwareBuild());
        sendChunk("</span>");
    }
    sendInfoRowEnd();
    sendInfoRow("Profile", Esp32Base::profileName());
    sendInfoRow("Hostname", Esp32Base::hostname());
    Esp32BaseLog::formatUptime(Esp32BaseSystem::uptimeMs(), value, sizeof(value));
    sendInfoRow("Uptime", value);
    snprintf(value, sizeof(value), "%lu", static_cast<unsigned long>(Esp32BaseSystem::bootCount()));
    sendInfoRow("Boot count", value);
    sendStatusSectionEnd();

    char mac[18];
    char flash[48];
    formatReadableBytes(Esp32BaseSystem::flashSize(), flash, sizeof(flash));
    sendStatusSectionStart("Hardware");
    sendInfoRow("Chip", ESP.getChipModel());
    snprintf(value, sizeof(value), "%d", ESP.getChipRevision());
    sendInfoRow("Revision", value);
    snprintf(value, sizeof(value), "%u MHz", static_cast<unsigned>(ESP.getCpuFreqMHz()));
    sendInfoRow("CPU", value);
    sendInfoRow("SDK", ESP.getSdkVersion());
    sendInfoRow("Flash chip", flash);
    if (ESP.getPsramSize() > 0) {
        char psramTotal[48];
        char psramFree[48];
        formatReadableBytes(ESP.getPsramSize(), psramTotal, sizeof(psramTotal));
        formatReadableBytes(ESP.getFreePsram(), psramFree, sizeof(psramFree));
        snprintf(value, sizeof(value), "free %s, total %s", psramFree, psramTotal);
        sendInfoRow("PSRAM", value);
    }
    formatMac(ESP.getEfuseMac(), mac, sizeof(mac));
    sendInfoRow("eFuse MAC", mac);
    sendStatusSectionEnd();

    sendStatusSectionStart("Firmware & OTA");
    char sketch[48];
    formatReadableBytes(ESP.getSketchSize(), sketch, sizeof(sketch));
    sendInfoRow("Current firmware", sketch);
    const esp_partition_t* runningPartition = esp_ota_get_running_partition();
    const esp_partition_t* nextPartition = esp_ota_get_next_update_partition(nullptr);
    if (runningPartition) {
        char runningSize[48];
        formatReadableBytes(runningPartition->size, runningSize, sizeof(runningSize));
        snprintf(value, sizeof(value), "%s / %s", runningPartition->label, runningSize);
        sendInfoRow("Running slot", value);
    } else {
        sendInfoRow("Running slot", "unknown");
    }
    if (nextPartition) {
        char targetSize[48];
        formatReadableBytes(nextPartition->size, targetSize, sizeof(targetSize));
        snprintf(value, sizeof(value), "%s / %s", nextPartition->label, targetSize);
        sendInfoRow("OTA target slot", value);
        sendInfoRow("Max OTA upload", targetSize);
        if (nextPartition->size >= ESP.getSketchSize()) {
            char headroom[48];
            formatReadableBytes(nextPartition->size - ESP.getSketchSize(), headroom, sizeof(headroom));
            sendInfoRow("OTA slot minus current sketch", headroom);
        } else {
            sendInfoRow("OTA slot minus current sketch", "0 B");
        }
    } else {
        sendInfoRow("OTA target slot", "unavailable");
        sendInfoRow("Max OTA upload", "unavailable");
        sendInfoRow("OTA slot minus current sketch", "unknown");
    }
#if ESP32BASE_ENABLE_OTA
    sendInfoRow("Rollback", Esp32BaseOta::isRollbackPossible() ? "possible" : "not possible");
    if (Esp32BaseOta::lastError()[0]) {
        sendInfoRow("Last OTA error", Esp32BaseOta::lastError());
    }
#endif
    sendStatusSectionEnd();

    sendStatusSectionStart("Runtime Health");
    char freeHeap[48];
    char minHeap[48];
    char maxAllocHeap[48];
    char totalHeap[48];
    formatReadableBytes(Esp32BaseSystem::freeHeap(), freeHeap, sizeof(freeHeap));
    formatReadableBytes(Esp32BaseSystem::minFreeHeap(), minHeap, sizeof(minHeap));
    formatReadableBytes(ESP.getMaxAllocHeap(), maxAllocHeap, sizeof(maxAllocHeap));
    formatReadableBytes(Esp32BaseSystem::totalHeap(), totalHeap, sizeof(totalHeap));
    snprintf(value, sizeof(value), "free %s, min %s, max alloc %s, total %s", freeHeap, minHeap, maxAllocHeap, totalHeap);
    sendInfoRow("Heap", value);
#if ESP32BASE_ENABLE_WATCHDOG
    const WatchdogTripState watchdogTrip = readWatchdogTripState();
    char tripResetAt[32];
    formatWatchdogTripResetAt(watchdogTrip, tripResetAt, sizeof(tripResetAt));
    sendInfoRowStart("Watchdog");
    if (watchdogTrip.invalidBase) {
        snprintf(value, sizeof(value), "enabled, lifetime resets %lu, trip resets invalid baseline",
                 static_cast<unsigned long>(watchdogTrip.lifetime));
    } else {
        snprintf(value, sizeof(value), "enabled, lifetime resets %lu, trip resets %lu",
                 static_cast<unsigned long>(watchdogTrip.lifetime),
                 static_cast<unsigned long>(watchdogTrip.trip));
    }
    sendEscapedHtmlChunk(value);
    sendChunk("<br>trip reset at ");
    sendEscapedHtmlChunk(tripResetAt);
    sendInfoRowEnd();
#endif
#if ESP32BASE_ENABLE_NTP
    const Esp32BaseNtp::TimeSnapshot time = Esp32BaseNtp::snapshot();
    sendInfoRowStart("Time");
    sendEscapedHtmlChunk(Esp32BaseNtp::isStarted() ? (time.synced ? "synced" : "pending") : "not started");
    if (time.synced && Esp32BaseNtp::formatTime(value, sizeof(value), "%Y-%m-%d %H:%M:%S")) {
        sendChunk("<br>");
        sendEscapedHtmlChunk(value);
    }
    sendInfoRowEnd();
#endif
    sendStatusSectionEnd();

#if ESP32BASE_ENABLE_WIFI
    sendStatusSectionStart("Network");
    sendInfoRowStart("WiFi");
    sendEscapedHtmlChunk(Esp32BaseWiFi::stateName());
    if (Esp32BaseWiFi::ssid()[0]) {
        sendChunk(" / ");
        sendEscapedHtmlChunk(Esp32BaseWiFi::ssid());
    }
    if (Esp32BaseWiFi::isConnected()) {
        char ip[24];
        if (Esp32BaseWiFi::ip(ip, sizeof(ip))) {
            sendChunk("<br>IP ");
            sendEscapedHtmlChunk(ip);
        }
        snprintf(value, sizeof(value), "<br>RSSI %ld dBm",
                 static_cast<long>(Esp32BaseWiFi::rssi()));
        sendChunk(value);
    }
    sendInfoRowEnd();
    sendInfoRow("Power save", Esp32BaseWiFi::powerSave() ? "on" : "off");
    sendInfoRow("STA MAC", WiFi.macAddress().c_str());
    sendInfoRow("AP MAC", WiFi.softAPmacAddress().c_str());
    sendStatusSectionEnd();
#endif

#if ESP32BASE_ENABLE_FS || ESP32BASE_ENABLE_FILELOG
    sendStatusSectionStart("Storage & Logs");
#if ESP32BASE_ENABLE_FS
    if (Esp32BaseFs::isReady()) {
        char used[48];
        char total[48];
        char freeBytes[48];
        formatReadableBytes(Esp32BaseFs::usedBytes(), used, sizeof(used));
        formatReadableBytes(Esp32BaseFs::totalBytes(), total, sizeof(total));
        formatReadableBytes(Esp32BaseFs::freeBytes(), freeBytes, sizeof(freeBytes));
        snprintf(value, sizeof(value), "used %s, free %s, total %s", used, freeBytes, total);
        sendInfoRow("FS", value);
    } else {
        sendInfoRow("FS", "unavailable");
    }
#endif
#if ESP32BASE_ENABLE_FILELOG
    sendInfoRowStart("File log");
    sendEscapedHtmlChunk(Esp32BaseFileLog::isEnabled() ? "enabled" : "disabled");
    if (Esp32BaseFileLog::isEnabled()) {
        char sizeBuf[48];
        formatReadableBytes(Esp32BaseFileLog::size(), sizeBuf, sizeof(sizeBuf));
        sendChunk("<br>");
        sendEscapedHtmlChunk(Esp32BaseFileLog::path());
        sendChunk(" / ");
        sendEscapedHtmlChunk(Esp32BaseFileLog::modeName());
        sendChunk(" / current ");
        sendEscapedHtmlChunk(sizeBuf);
    }
    sendInfoRowEnd();
#endif
    sendStatusSectionEnd();
#endif

    sendPartitionTable();

    sendStatusSectionStart("Boot Reasons");
    sendInfoRowStart("Reset reason");
    sendEscapedHtmlChunk(Esp32BaseSystem::resetReason());
    sendChunk(" / ");
    sendEscapedHtmlChunk(Esp32BaseSystem::resetReasonText());
    sendInfoRowEnd();
    sendInfoRowStart("Wake reason");
    sendEscapedHtmlChunk(Esp32BaseSystem::wakeReason());
    sendChunk(" / ");
    sendEscapedHtmlChunk(Esp32BaseSystem::wakeReasonText());
    sendInfoRowEnd();
    sendStatusSectionEnd();

    if (g_homeMode == Esp32BaseWeb::HOME_ESP32BASE && appNavCount() > 0) {
        sendChunk("<section class='panel'><h2>Application</h2>");
        sendAppLinks(true, nullptr);
        sendChunk("</section>");
    }
    Esp32BaseWeb::sendFooter();
}

#if ESP32BASE_ENABLE_FILELOG
uint8_t selectedLogSegment() {
    if (!g_server.hasArg("segment")) {
        return 0;
    }
    const String raw = g_server.arg("segment");
    if (raw.length() == 0 || raw.length() > 3) {
        return 0;
    }
    uint16_t value = 0;
    for (size_t i = 0; i < raw.length(); ++i) {
        const char c = raw.charAt(i);
        if (c < '0' || c > '9') {
            return 0;
        }
        value = static_cast<uint16_t>(value * 10U + static_cast<uint16_t>(c - '0'));
    }
    if (value >= Esp32BaseFileLog::rotateFiles()) {
        return 0;
    }
    return static_cast<uint8_t>(value);
}

void sendRawLogChunk(const char* data, size_t len, void* user) {
    (void)user;
    sendChunk(data, len);
#if ESP32BASE_ENABLE_WATCHDOG
    Esp32BaseWatchdog::feed();
#endif
    yield();
}

void logSegmentTitle(uint8_t index, char* out, size_t len) {
    if (!out || len == 0) {
        return;
    }
    if (index == 0) {
        snprintf(out, len, "current-0");
        return;
    }
    snprintf(out, len, "history-%u", static_cast<unsigned>(index));
}

void sendLogSegmentTabs(uint8_t selected) {
    char title[16];
    char sizeBuf[48];
    char line[160];
    sendChunk("<div class='tabs'>");
    for (uint8_t i = 0; i < Esp32BaseFileLog::rotateFiles(); ++i) {
        logSegmentTitle(i, title, sizeof(title));
        formatReadableBytes(Esp32BaseFileLog::segmentSize(i), sizeBuf, sizeof(sizeBuf));
        snprintf(line, sizeof(line), "<a%s href='/esp32base/logs?segment=%u'>",
                 i == selected ? " class='active'" : "",
                 static_cast<unsigned>(i));
        sendChunk(line);
        sendChunk("<span class='segname'>");
        sendEscapedHtmlChunk(title);
        sendChunk("</span><span class='segsize'>");
        sendEscapedHtmlChunk(sizeBuf);
        sendChunk("</span></a>");
    }
    sendChunk("</div>");
}
#endif

void handleLogsPage() {
    markRequest();
    if (!ensureAuth()) {
        return;
    }
    Esp32BaseWeb::sendHeader(g_builtinLabels[Esp32BaseWeb::BUILTIN_LOGS]);
    sendChunk("<header class='pagehead'><h1>");
    sendEscapedHtmlChunk(g_builtinLabels[Esp32BaseWeb::BUILTIN_LOGS]);
    sendChunk("</h1></header>");
#if ESP32BASE_ENABLE_FILELOG
    Esp32BaseFileLog::flush();
    char maxBuf[48];
    char totalBuf[48];
    char bufferUsed[48];
    char bufferTotal[48];
    formatReadableBytes(Esp32BaseFileLog::maxBytes(), maxBuf, sizeof(maxBuf));
    formatReadableBytes(static_cast<uint64_t>(Esp32BaseFileLog::maxBytes()) * Esp32BaseFileLog::rotateFiles(), totalBuf, sizeof(totalBuf));
    formatReadableBytes(Esp32BaseFileLog::bufferUsed(), bufferUsed, sizeof(bufferUsed));
    formatReadableBytes(Esp32BaseFileLog::bufferSize(), bufferTotal, sizeof(bufferTotal));
    char line[320];
    sendChunk("<section class='panel'><table class='logmeta'><tr><th>File log</th><td><b>");
    sendEscapedHtmlChunk(Esp32BaseFileLog::isEnabled() ? "enabled" : "disabled");
    sendChunk("</b></td></tr><tr><th>Path</th><td>");
    sendEscapedHtmlChunk(Esp32BaseFileLog::path());
    snprintf(line, sizeof(line), "</td></tr><tr><th>Rotation files</th><td>%u</td></tr><tr><th>Mode</th><td>%s</td></tr><tr><th>Buffer</th><td>%s / %s</td></tr><tr><th>Flush interval</th><td>%lu ms</td></tr><tr><th>Max per file</th><td>%s</td></tr><tr><th>Max total</th><td>%s</td></tr><tr><th>Segments</th><td>",
             static_cast<unsigned>(Esp32BaseFileLog::rotateFiles()),
             Esp32BaseFileLog::modeName(),
             bufferUsed,
             bufferTotal,
             static_cast<unsigned long>(Esp32BaseFileLog::flushIntervalMs()),
             maxBuf,
             totalBuf);
    sendChunk(line);
    for (int8_t i = static_cast<int8_t>(Esp32BaseFileLog::rotateFiles()) - 1; i >= 0; --i) {
        char sizeBuf[48];
        formatReadableBytes(Esp32BaseFileLog::segmentSize(static_cast<uint8_t>(i)), sizeBuf, sizeof(sizeBuf));
        snprintf(line, sizeof(line), "%s%u=%s", i == static_cast<int8_t>(Esp32BaseFileLog::rotateFiles()) - 1 ? "" : ", ", static_cast<unsigned>(i), sizeBuf);
        sendChunk(line);
    }
    const uint8_t selectedSegment = selectedLogSegment();
    sendChunk("</td></tr></table></section><section class='panel'>");
    sendLogSegmentTabs(selectedSegment);
    snprintf(line, sizeof(line), "<p><a href='/esp32base/logs/raw?segment=%u'>Open raw log</a></p><iframe class='logframe' src='/esp32base/logs/raw?segment=%u'></iframe>",
             static_cast<unsigned>(selectedSegment),
             static_cast<unsigned>(selectedSegment));
    sendChunk(line);
    sendChunk("</section>");
#else
    sendChunk("<section class='panel'><p>File log: <b>unavailable</b></p></section>");
#endif
    Esp32BaseWeb::sendFooter();
}

void handleLogsRaw() {
    markRequest();
    if (!ensureAuth()) {
        return;
    }
#if ESP32BASE_ENABLE_FILELOG
    Esp32BaseFileLog::flush();
    const uint8_t selectedSegment = selectedLogSegment();
    g_server.sendHeader("X-Content-Type-Options", "nosniff");
    if (!beginResponse(200, "text/plain; charset=utf-8", nullptr)) {
        return;
    }
    if (!Esp32BaseFileLog::streamSegment(selectedSegment, sendRawLogChunk)) {
        sendChunk("log unavailable\n");
    }
    endResponse();
#else
    g_server.send(404, "text/plain", "File log unavailable");
#endif
}

void handleLogsClear() {
    markRequest();
    if (!ensurePostAllowed("logs_clear")) {
        return;
    }
#if ESP32BASE_ENABLE_FILELOG
    const bool ok = Esp32BaseFileLog::clear();
    redirectSeeOther(ok ? "/esp32base/logs?cleared=1" : "/esp32base/logs?error=clear_failed");
#else
    redirectSeeOther("/esp32base/logs?error=unavailable");
#endif
}

void handleAuthPage() {
    markRequest();
    if (!ensureAuth()) {
        return;
    }
    Esp32BaseWeb::sendHeader(g_builtinLabels[Esp32BaseWeb::BUILTIN_AUTH]);
    sendChunk("<header class='pagehead'><h1>");
    sendEscapedHtmlChunk(g_builtinLabels[Esp32BaseWeb::BUILTIN_AUTH]);
    sendChunk("</h1></header>");
    if (g_server.hasArg("saved")) {
        sendChunk("<p class='ok'>Auth updated. Use the new credentials on the next auth prompt.</p>");
    } else if (g_server.hasArg("error")) {
        sendChunk("<p class='err'>Auth update failed: ");
        sendEscapedHtmlChunk(g_server.arg("error").c_str());
        sendChunk("</p>");
    }
    sendChunk("<section class='panel'><form method='post' action='/esp32base/auth' onsubmit=\"var f=this;function v(n){return f.elements[n].value.trim();}if(!v('current_user')||!v('current_pass')||!v('new_user')||!v('new_pass')||!v('confirm_pass')){alert('All auth fields are required');return false;}if(v('new_pass')!=v('confirm_pass')){alert('New passwords do not match');return false;}return once(this)\">");
    sendChunk("<label for='current_user'>Current auth user</label><input id='current_user' name='current_user' maxlength='31' required value='");
    sendEscapedHtmlChunk(g_authUser);
    sendChunk("'>");
    sendChunk("<label for='current_pass'>Current auth password</label><input id='current_pass' type='password' name='current_pass' maxlength='63' autocomplete='current-password' required>");
    sendChunk("<label for='new_user'>New auth user</label><input id='new_user' name='new_user' maxlength='31' required value='");
    sendEscapedHtmlChunk(g_authUser);
    sendChunk("'>");
    sendChunk("<label for='new_pass'>New auth password</label><input id='new_pass' type='password' name='new_pass' maxlength='63' autocomplete='new-password' required>");
    sendChunk("<label for='confirm_pass'>Confirm new auth password</label><input id='confirm_pass' type='password' name='confirm_pass' maxlength='63' autocomplete='new-password' required>");
    sendChunk("<input type='submit' value='Save Auth'></form></section>");
    Esp32BaseWeb::sendFooter();
}

void handleAuthSubmit() {
    markRequest();
    if (!ensurePostAllowed("auth_submit")) {
        return;
    }
    char currentUser[32];
    char currentPass[64];
    char newUser[32];
    char newPass[64];
    char confirmPass[64];
    strlcpy(currentUser, g_server.arg("current_user").c_str(), sizeof(currentUser));
    strlcpy(currentPass, g_server.arg("current_pass").c_str(), sizeof(currentPass));
    strlcpy(newUser, g_server.arg("new_user").c_str(), sizeof(newUser));
    strlcpy(newPass, g_server.arg("new_pass").c_str(), sizeof(newPass));
    strlcpy(confirmPass, g_server.arg("confirm_pass").c_str(), sizeof(confirmPass));
    if (!Esp32BaseWeb::verifyAuth(currentUser, currentPass)) {
        ESP32BASE_LOG_W("web", "auth_update_failed reason=current_auth");
        redirectSeeOther("/esp32base/auth?error=current_auth");
        return;
    }
    if (strcmp(newPass, confirmPass) != 0) {
        ESP32BASE_LOG_W("web", "auth_update_failed reason=confirm");
        redirectSeeOther("/esp32base/auth?error=confirm");
        return;
    }
    if (!Esp32BaseWeb::saveAuth(newUser, newPass)) {
        redirectSeeOther("/esp32base/auth?error=invalid_or_storage");
        return;
    }
    redirectSeeOther("/esp32base/auth?saved=1");
}
}

bool Esp32BaseWeb::begin() {
    if (g_webReady) {
        return true;
    }
    const uint8_t appRoutes = routeCount(false);
    const uint8_t appPages = routeCount(true);
    uint8_t builtinRoutes = 24;
#if ESP32BASE_ENABLE_OTA
    builtinRoutes += 3;
#endif
    ESP32BASE_LOG_D("web", "server_registering builtin_routes=%u app_routes=%u app_pages=%u",
                    static_cast<unsigned>(builtinRoutes),
                    static_cast<unsigned>(appRoutes),
                    static_cast<unsigned>(appPages));
    if (!loadStoredAuth()) {
        applyDefaultAuth();
        ESP32BASE_LOG_I("web", "auth_loaded user=%s password=%s source=default", g_authUser, g_authPass);
    }
    g_server.collectHeaders(g_headerKeys, sizeof(g_headerKeys) / sizeof(g_headerKeys[0]));
    g_server.on("/", HTTP_GET, handleRootRedirect);
    g_server.on("/esp32base", HTTP_GET, handleRoot);
    g_server.on("/esp32base/api/status", HTTP_GET, handleStatus);
    g_server.on("/esp32base/api/chip", HTTP_GET, handleChip);
    g_server.on("/esp32base/api/firmware", HTTP_GET, handleFirmware);
    g_server.on("/esp32base/api/hostname", HTTP_GET, handleHostnameApiGet);
    g_server.on("/esp32base/api/hostname", HTTP_POST, handleHostnameSubmit);
    g_server.on("/esp32base/wifi", HTTP_GET, handleWifiPage);
    g_server.on("/esp32base/wifi", HTTP_POST, handleWifiSubmit);
    g_server.on("/esp32base/api/wifi", HTTP_POST, handleWifiSubmit);
    g_server.on("/esp32base/api/wifi/clear", HTTP_POST, handleWifiClear);
    g_server.on("/esp32base/logs", HTTP_GET, handleLogsPage);
    g_server.on("/esp32base/logs/raw", HTTP_GET, handleLogsRaw);
    g_server.on("/esp32base/logs/clear", HTTP_POST, handleLogsClear);
    g_server.on("/esp32base/auth", HTTP_GET, handleAuthPage);
    g_server.on("/esp32base/auth", HTTP_POST, handleAuthSubmit);
    g_server.on("/esp32base/tools", HTTP_GET, handleToolsPage);
#if ESP32BASE_ENABLE_APP_CONFIG
    g_server.on("/esp32base/app-config", HTTP_GET, handleAppConfigPage);
    g_server.on("/esp32base/app-config", HTTP_POST, handleAppConfigSubmit);
#endif
    g_server.on("/esp32base/tools/hostname", HTTP_POST, handleHostnameSubmit);
    g_server.on("/esp32base/tools/filelog", HTTP_POST, handleToolsFileLogPost);
    g_server.on("/esp32base/tools/reboot", HTTP_POST, handleToolsRebootPost);
#if ESP32BASE_ENABLE_WATCHDOG
    g_server.on("/esp32base/tools/watchdog-trip-reset", HTTP_POST, handleToolsWatchdogTripResetPost);
#endif
    g_server.on("/esp32base/tools/format-fs", HTTP_POST, handleToolsFormatFsPost);
    g_server.on("/esp32base/tools/logs-clear", HTTP_POST, handleToolsLogsClearPost);
    g_server.on("/esp32base/api/restart", HTTP_POST, handleRestart);
    g_server.on("/generate_204", HTTP_GET, handleCaptiveProbe);
    g_server.on("/hotspot-detect.html", HTTP_GET, handleCaptiveProbe);
    g_server.on("/ncsi.txt", HTTP_GET, handleCaptiveProbe);
    g_server.on("/favicon.ico", HTTP_GET, handleNoContent);
#if ESP32BASE_ENABLE_OTA
    g_server.on("/esp32base/ota", HTTP_GET, handleOtaPage);
    g_server.on("/esp32base/ota", HTTP_POST, handleOtaUploadDone, handleOtaUpload);
    g_server.on("/esp32base/ota/raw", HTTP_POST, handleOtaUploadDone, handleOtaRawUpload);
    g_server.on("/esp32base/api/ota", HTTP_GET, handleOtaApi);
#endif
    for (uint8_t i = 0; i < ESP32BASE_WEB_MAX_ROUTES; ++i) {
        if (g_routes[i].handler && !g_routes[i].registered) {
            registerRoute(g_routes[i]);
        }
    }
    g_server.onNotFound(handleNotFound);
    g_server.begin();
    g_webReady = true;
    ESP32BASE_LOG_I("web", "server ready");
#if ESP32BASE_ENABLE_BUS
    Esp32BaseBus::publish(EVENT_READY, "");
#endif
    return true;
}

void Esp32BaseWeb::handle() {
    if (g_webReady) {
        g_activeUri[0] = '\0';
        g_currentMethod = METHOD_UNKNOWN;
        g_lastRequestMethod = METHOD_UNKNOWN;
        g_requestContextActive = false;
        g_responseActive = false;
        g_authLoggedForRequest = false;
        const uint32_t start = millis();
        g_server.handleClient();
        const uint32_t elapsed = millis() - start;
        if (elapsed > 250UL) {
            if (g_lastRequestMethod == METHOD_GET) {
                ESP32BASE_LOG_D("web", "slow_request method=%s uri=%s elapsed=%lu ms",
                                methodName(g_lastRequestMethod),
                                g_activeUri[0] ? g_activeUri : "(unknown)",
                                static_cast<unsigned long>(elapsed));
            } else {
                ESP32BASE_LOG_W("web", "slow_request method=%s uri=%s elapsed=%lu ms",
                                methodName(g_lastRequestMethod),
                                g_activeUri[0] ? g_activeUri : "(unknown)",
                                static_cast<unsigned long>(elapsed));
            }
        }
        g_requestContextActive = false;
        g_currentMethod = METHOD_UNKNOWN;
        g_responseActive = false;
    }
}

bool Esp32BaseWeb::isReady() {
    return g_webReady;
}

void Esp32BaseWeb::setDefaultAuth(const char* user, const char* pass) {
    if (!validAuthUser(user) || !validAuthPass(pass)) {
        ESP32BASE_LOG_W("web", "set_default_auth_failed reason=invalid");
        return;
    }
    strlcpy(g_defaultAuthUser, user, sizeof(g_defaultAuthUser));
    strlcpy(g_defaultAuthPass, pass, sizeof(g_defaultAuthPass));
    g_defaultAuthSet = true;
    if (!g_authLoadedFromStorage) {
        applyDefaultAuth();
    }
    ESP32BASE_LOG_I("web", "default_auth_set user=%s password=%s applied=%s",
                    g_defaultAuthUser,
                    g_defaultAuthPass,
                    g_authLoadedFromStorage ? "no" : "yes");
}

const char* Esp32BaseWeb::authUser() {
    return g_authUser;
}

const char* Esp32BaseWeb::authPassword() {
    return g_authPass;
}

bool Esp32BaseWeb::isAuthEnabled() {
    return g_authEnabled;
}

void Esp32BaseWeb::setAuthEnabled(bool enabled) {
    g_authEnabled = enabled;
    ESP32BASE_LOG_W("web", "auth_%s builtin_routes=%s",
                    enabled ? "enabled" : "disabled",
                    enabled ? "protected" : "open");
}

bool Esp32BaseWeb::checkAuth() {
    return ensureAuth();
}

bool Esp32BaseWeb::verifyAuth() {
    return isAuthenticated();
}

bool Esp32BaseWeb::verifyAuth(const char* user, const char* pass) {
    return authMatches(user, pass);
}

bool Esp32BaseWeb::saveAuth(const char* user, const char* pass) {
    if (!validAuthUser(user)) {
        ESP32BASE_LOG_W("web", "auth_update_failed reason=invalid_user");
        return false;
    }
    if (!validAuthPass(pass)) {
        ESP32BASE_LOG_W("web", "auth_update_failed reason=invalid_password");
        return false;
    }
    if (!saveStoredAuth(user, pass)) {
        ESP32BASE_LOG_W("web", "auth_update_failed reason=storage_failed");
        return false;
    }
    applyStoredAuth(user, pass);
    ESP32BASE_LOG_I("web", "auth_saved_plain user=%s password=%s", user, pass);
    ESP32BASE_LOG_I("web", "auth_saved user=%s", g_authUser);
    return true;
}

bool Esp32BaseWeb::resetAuth() {
    const bool ok = Esp32BaseConfig::clearNamespace("eb_web");
    applyDefaultAuth();
    ESP32BASE_LOG_I("web", "auth_loaded user=%s password=%s source=default", g_authUser, g_authPass);
    ESP32BASE_LOG_I("web", "auth_reset");
    return ok;
}

bool Esp32BaseWeb::addRoute(const char* path, Method method, Handler handler) {
    if (!path || path[0] != '/' || !handler || strlen(path) >= sizeof(g_routes[0].path) ||
        (method != METHOD_GET && method != METHOD_POST && method != METHOD_ANY)) {
        return false;
    }
    for (uint8_t i = 0; i < ESP32BASE_WEB_MAX_ROUTES; ++i) {
        if (!g_routes[i].handler) {
            strlcpy(g_routes[i].path, path, sizeof(g_routes[i].path));
            g_routes[i].title[0] = '\0';
            g_routes[i].method = method;
            g_routes[i].handler = handler;
            g_routes[i].appPage = false;
            if (g_webReady) {
                registerRoute(g_routes[i]);
            }
            return true;
        }
    }
    return false;
}

bool Esp32BaseWeb::addPage(const char* path, const char* title, Handler handler) {
    if (!path || path[0] != '/' || !title || !title[0] || !handler ||
        strlen(path) >= sizeof(g_routes[0].path) || strlen(title) >= sizeof(g_routes[0].title)) {
        return false;
    }
    for (uint8_t i = 0; i < ESP32BASE_WEB_MAX_ROUTES; ++i) {
        if (!g_routes[i].handler) {
            strlcpy(g_routes[i].path, path, sizeof(g_routes[i].path));
            strlcpy(g_routes[i].title, title, sizeof(g_routes[i].title));
            g_routes[i].method = METHOD_GET;
            g_routes[i].handler = handler;
            g_routes[i].appPage = true;
            if (g_webReady) {
                registerRoute(g_routes[i]);
            }
            ESP32BASE_LOG_D("web", "app_page_registered path=%s title=%s", g_routes[i].path, g_routes[i].title);
            return true;
        }
    }
    return false;
}

bool Esp32BaseWeb::addApi(const char* path, Handler handler) {
    return addRoute(path, METHOD_ANY, handler);
}

bool Esp32BaseWeb::addNavItem(const char* path, const char* title) {
    if (!path || path[0] != '/' || !title || !title[0] ||
        strlen(path) >= sizeof(g_navItems[0].path) || strlen(title) >= sizeof(g_navItems[0].title)) {
        return false;
    }
    for (uint8_t i = 0; i < ESP32BASE_WEB_MAX_NAV_ITEMS; ++i) {
        if (!g_navItems[i].path[0]) {
            strlcpy(g_navItems[i].path, path, sizeof(g_navItems[i].path));
            strlcpy(g_navItems[i].title, title, sizeof(g_navItems[i].title));
            ESP32BASE_LOG_D("web", "nav_item_registered path=%s title=%s", g_navItems[i].path, g_navItems[i].title);
            return true;
        }
    }
    return false;
}

bool Esp32BaseWeb::setDeviceName(const char* name) {
    if (!name || !name[0] || strlen(name) >= sizeof(g_deviceName)) {
        return false;
    }
    strlcpy(g_deviceName, name, sizeof(g_deviceName));
    return true;
}

bool Esp32BaseWeb::setHomePath(const char* path) {
    if (!path || path[0] != '/' || strlen(path) >= sizeof(g_homePath)) {
        return false;
    }
    strlcpy(g_homePath, path, sizeof(g_homePath));
    return true;
}

void Esp32BaseWeb::setHomeMode(HomeMode mode) {
    if (mode == HOME_ESP32BASE || mode == HOME_APP || mode == HOME_COMBINED) {
        g_homeMode = mode;
    }
}

void Esp32BaseWeb::setSystemNavMode(SystemNavMode mode) {
    if (mode == SYSTEM_NAV_TOP || mode == SYSTEM_NAV_BOTTOM || mode == SYSTEM_NAV_SECTION) {
        g_systemNavMode = mode;
    }
}

bool Esp32BaseWeb::setBuiltinLabel(BuiltinPage page, const char* label) {
    if (page > BUILTIN_AUTH || !label || !label[0] || strlen(label) >= sizeof(g_builtinLabels[0])) {
        return false;
    }
    strlcpy(g_builtinLabels[page], label, sizeof(g_builtinLabels[page]));
    return true;
}

void Esp32BaseWeb::setHeadExtraCallback(Handler handler) {
    g_headExtraCallback = handler;
}

Esp32BaseWeb::Method Esp32BaseWeb::currentMethod() {
    return g_requestContextActive ? g_currentMethod : METHOD_UNKNOWN;
}

bool Esp32BaseWeb::isMethod(Method method) {
    const Method current = currentMethod();
    if (method == METHOD_ANY) {
        return current != METHOD_UNKNOWN;
    }
    return current == method;
}

const char* Esp32BaseWeb::currentMethodName() {
    return methodName(currentMethod());
}

bool Esp32BaseWeb::hasParam(const char* name) {
    return name && g_server.hasArg(name);
}

bool Esp32BaseWeb::getParam(const char* name, char* out, size_t len) {
    if (!name || !out || len == 0 || !g_server.hasArg(name)) {
        return false;
    }
    strlcpy(out, g_server.arg(name).c_str(), len);
    return true;
}

bool Esp32BaseWeb::getRequestBody(char* out, size_t len) {
    if (!out || len == 0) {
        return false;
    }
    strlcpy(out, g_server.arg("plain").c_str(), len);
    return true;
}

void Esp32BaseWeb::sendHeader(const char* title) {
    if (!beginResponse(200, "text/html; charset=utf-8", nullptr)) {
        return;
    }
    sendChunk("<!doctype html><html><head><title>");
    sendEscapedHtmlChunk(title ? title : g_deviceName);
    sendChunk("</title>");
    sendProgmem(WEB_HEAD);
    if (g_pageHeadProgmem) {
        sendProgmem(g_pageHeadProgmem);
        g_pageHeadProgmem = nullptr;
    }
    if (g_headExtraCallback) {
        g_headExtraCallback();
    }
    sendChunk("</head><body>");
    sendMainNav();
    sendChunk("<main class='page'>");
}

void Esp32BaseWeb::sendFooter() {
    if (g_systemNavMode == Esp32BaseWeb::SYSTEM_NAV_SECTION) {
        sendChunk("</main>");
        sendChunk("<footer class='footerbar'><span class='syslinks'>");
        sendSystemLinks(false, activeNavPath(true));
        sendChunk("</span><span class='heap'>");
        sendFooterStats();
        sendChunk("</span></footer></body></html>");
    } else {
        sendChunk("</main>");
        sendSystemNavSection();
        sendChunk("<div class=info>");
        sendFooterStats();
        sendChunk("</div></body></html>");
    }
    endResponse();
}

bool Esp32BaseWeb::beginResponse(int code, const char* contentType, const char* filename) {
    return ::beginResponse(code, contentType, filename);
}

bool Esp32BaseWeb::beginText(int code) {
    return beginResponse(code, "text/plain; charset=utf-8", nullptr);
}

bool Esp32BaseWeb::beginCsv(int code, const char* filename) {
    return beginResponse(code, "text/csv; charset=utf-8", filename);
}

void Esp32BaseWeb::endResponse() {
    ::endResponse();
}

void Esp32BaseWeb::sendChunk(const char* text) {
    ::sendChunk(text);
}

void Esp32BaseWeb::sendBytes(const uint8_t* data, size_t len) {
    if (!data && len > 0) {
        ESP32BASE_LOG_W("web", "send_bytes invalid data");
        return;
    }
    if (!g_responseActive) {
        ESP32BASE_LOG_W("web", "send_bytes outside response");
        return;
    }
    if (g_responseBroken) {
        return;
    }
    flushChunkBuffer();
    if (!g_responseBroken && len > 0) {
        sendResponseContent(reinterpret_cast<const char*>(data), len);
    }
}

void Esp32BaseWeb::writeHtmlEscaped(const char* text) {
    sendEscapedHtmlChunk(text);
}

void Esp32BaseWeb::writeCsvEscaped(const char* text) {
    sendChunk("\"");
    if (text) {
        for (const char* p = text; *p; ++p) {
            if (*p == '"') {
                sendChunk("\"\"");
            } else {
                char one[2] = {*p, '\0'};
                sendChunk(one);
            }
        }
    }
    sendChunk("\"");
}

void Esp32BaseWeb::sendText(int code, const char* text) {
    g_server.send(code, "text/plain", text ? text : "");
}

void Esp32BaseWeb::sendHtml(int code, const char* html) {
    g_server.send(code, "text/html", html ? html : "");
}

void Esp32BaseWeb::sendJson(int code, const char* json) {
    g_server.send(code, "application/json", json ? json : "{}");
}

void Esp32BaseWeb::redirectSeeOther(const char* url) {
    ::redirectSeeOther(url ? url : "/esp32base");
}

void Esp32BaseWeb::beginJson(int code) {
    if (!beginResponse(code, "application/json", nullptr)) {
        return;
    }
    sendChunk("{");
}

void Esp32BaseWeb::writeJsonEscaped(const char* text) {
    if (!text) {
        return;
    }
    sendEscapedJsonChunk(text);
}

void Esp32BaseWeb::endJson() {
    sendChunk("}");
    endResponse();
}

#if ESP32BASE_ENABLE_APP_CONFIG
bool Esp32BaseAppConfig::setTitle(const char* title) {
    if (!title || !title[0] || strlen(title) >= sizeof(g_appConfigTitle)) {
        return false;
    }
    strlcpy(g_appConfigTitle, title, sizeof(g_appConfigTitle));
    return true;
}

bool Esp32BaseAppConfig::setPageValidateCallback(PageValidateCallback callback) {
    g_appConfigPageValidate = callback;
    return true;
}

bool Esp32BaseAppConfig::setChangeCallback(ChangeCallback callback) {
    g_appConfigChangeCallback = callback;
    return true;
}

bool Esp32BaseAppConfig::setSaveCallback(SaveCallback callback) {
    g_appConfigSaveCallback = callback;
    return true;
}

bool Esp32BaseAppConfig::addGroup(const Group& group) {
    if (!group.id || !group.id[0] || strlen(group.id) > 15 || !group.title || !group.title[0] ||
        strlen(group.title) > LABEL_MAX_LENGTH ||
        g_appConfigGroupCount >= ESP32BASE_APP_CONFIG_MAX_GROUPS || groupExists(group.id)) {
        ESP32BASE_LOG_W("appcfg", "group_register_failed id=%s", group.id ? group.id : "-");
        return false;
    }
    AppConfigGroupSlot& slot = g_appConfigGroups[g_appConfigGroupCount++];
    slot.used = true;
    slot.id = group.id;
    slot.title = group.title;
    return true;
}

bool Esp32BaseAppConfig::addString(const StringField& field) {
    if (g_appConfigFieldCount >= ESP32BASE_APP_CONFIG_MAX_FIELDS ||
        !validFieldCommon(field.groupId, field.ns, field.key, field.label) ||
        !validOptionalHelp(field.help) ||
        field.minLength > field.maxLength || field.maxLength > STRING_MAX_LENGTH ||
        !field.defaultValue || strlen(field.defaultValue) < field.minLength || strlen(field.defaultValue) > field.maxLength) {
        appConfigLogRegisterFailed("string", field.ns, field.key);
        return false;
    }
    AppConfigFieldSlot& slot = g_appConfigFields[g_appConfigFieldCount++];
    memset(&slot, 0, sizeof(slot));
    slot.used = true;
    slot.type = APP_CFG_STRING;
    slot.groupId = field.groupId;
    slot.ns = field.ns;
    slot.key = field.key;
    slot.label = field.label;
    slot.help = field.help;
    slot.restartRequired = field.restartRequired;
    slot.validate = field.validate;
    slot.spec.stringField = field;
    return true;
}

bool Esp32BaseAppConfig::addInt(const IntField& field) {
    if (g_appConfigFieldCount >= ESP32BASE_APP_CONFIG_MAX_FIELDS ||
        !validFieldCommon(field.groupId, field.ns, field.key, field.label) ||
        !validOptionalHelp(field.help) || !validOptionalUnit(field.unit) ||
        field.minValue > field.maxValue || field.defaultValue < field.minValue || field.defaultValue > field.maxValue ||
        field.step <= 0 || !stepMatches(field.defaultValue, field.minValue, field.step)) {
        appConfigLogRegisterFailed("int", field.ns, field.key);
        return false;
    }
    AppConfigFieldSlot& slot = g_appConfigFields[g_appConfigFieldCount++];
    memset(&slot, 0, sizeof(slot));
    slot.used = true;
    slot.type = APP_CFG_INT;
    slot.groupId = field.groupId;
    slot.ns = field.ns;
    slot.key = field.key;
    slot.label = field.label;
    slot.help = field.help;
    slot.unit = field.unit;
    slot.restartRequired = field.restartRequired;
    slot.validate = field.validate;
    slot.spec.intField = field;
    return true;
}

bool Esp32BaseAppConfig::addDecimal(const DecimalField& field) {
    if (g_appConfigFieldCount >= ESP32BASE_APP_CONFIG_MAX_FIELDS ||
        !validFieldCommon(field.groupId, field.ns, field.key, field.label) ||
        !validOptionalHelp(field.help) || !validOptionalUnit(field.unit) ||
        field.scale > DECIMAL_SCALE_MAX || field.minRawValue > field.maxRawValue ||
        field.defaultRawValue < field.minRawValue || field.defaultRawValue > field.maxRawValue ||
        field.stepRaw <= 0 || !stepMatches(field.defaultRawValue, field.minRawValue, field.stepRaw)) {
        appConfigLogRegisterFailed("decimal", field.ns, field.key);
        return false;
    }
    AppConfigFieldSlot& slot = g_appConfigFields[g_appConfigFieldCount++];
    memset(&slot, 0, sizeof(slot));
    slot.used = true;
    slot.type = APP_CFG_DECIMAL;
    slot.groupId = field.groupId;
    slot.ns = field.ns;
    slot.key = field.key;
    slot.label = field.label;
    slot.help = field.help;
    slot.unit = field.unit;
    slot.restartRequired = field.restartRequired;
    slot.validate = field.validate;
    slot.spec.decimalField = field;
    return true;
}

bool Esp32BaseAppConfig::addBool(const BoolField& field) {
    if (g_appConfigFieldCount >= ESP32BASE_APP_CONFIG_MAX_FIELDS ||
        !validFieldCommon(field.groupId, field.ns, field.key, field.label) ||
        !validOptionalHelp(field.help)) {
        appConfigLogRegisterFailed("bool", field.ns, field.key);
        return false;
    }
    AppConfigFieldSlot& slot = g_appConfigFields[g_appConfigFieldCount++];
    memset(&slot, 0, sizeof(slot));
    slot.used = true;
    slot.type = APP_CFG_BOOL;
    slot.groupId = field.groupId;
    slot.ns = field.ns;
    slot.key = field.key;
    slot.label = field.label;
    slot.help = field.help;
    slot.restartRequired = field.restartRequired;
    slot.validate = field.validate;
    slot.spec.boolField = field;
    return true;
}

bool Esp32BaseAppConfig::addEnum(const EnumField& field) {
    if (g_appConfigFieldCount >= ESP32BASE_APP_CONFIG_MAX_FIELDS ||
        !validFieldCommon(field.groupId, field.ns, field.key, field.label) ||
        !validOptionalHelp(field.help) || !validEnumOptions(field)) {
        appConfigLogRegisterFailed("enum", field.ns, field.key);
        return false;
    }
    AppConfigFieldSlot& slot = g_appConfigFields[g_appConfigFieldCount++];
    memset(&slot, 0, sizeof(slot));
    slot.used = true;
    slot.type = APP_CFG_ENUM;
    slot.groupId = field.groupId;
    slot.ns = field.ns;
    slot.key = field.key;
    slot.label = field.label;
    slot.help = field.help;
    slot.restartRequired = field.restartRequired;
    slot.validate = field.validate;
    slot.spec.enumField = field;
    return true;
}

bool Esp32BaseAppConfig::submittedString(const char* ns, const char* key, char* out, size_t len) {
    const AppConfigFieldSlot* field = nullptr;
    return g_appConfigSubmitContext && findSubmittedRaw(ns, key, out, len, &field) && field && field->type == APP_CFG_STRING;
}

bool Esp32BaseAppConfig::submittedInt(const char* ns, const char* key, int32_t& out) {
    char raw[32];
    const AppConfigFieldSlot* field = nullptr;
    return g_appConfigSubmitContext && findSubmittedRaw(ns, key, raw, sizeof(raw), &field) && field && field->type == APP_CFG_INT && parseStrictInt32(raw, out);
}

bool Esp32BaseAppConfig::submittedDecimal(const char* ns, const char* key, int32_t& rawOut) {
    char raw[32];
    const AppConfigFieldSlot* field = nullptr;
    return g_appConfigSubmitContext && findSubmittedRaw(ns, key, raw, sizeof(raw), &field) && field && field->type == APP_CFG_DECIMAL &&
           parseDecimalRaw(raw, field->spec.decimalField.scale, rawOut);
}

bool Esp32BaseAppConfig::submittedBool(const char* ns, const char* key, bool& out) {
    char raw[4];
    const AppConfigFieldSlot* field = nullptr;
    if (!(g_appConfigSubmitContext && findSubmittedRaw(ns, key, raw, sizeof(raw), &field) && field && field->type == APP_CFG_BOOL)) {
        return false;
    }
    out = strcmp(raw, "1") == 0;
    return true;
}

bool Esp32BaseAppConfig::submittedEnum(const char* ns, const char* key, char* out, size_t len) {
    const AppConfigFieldSlot* field = nullptr;
    return g_appConfigSubmitContext && findSubmittedRaw(ns, key, out, len, &field) && field && field->type == APP_CFG_ENUM;
}
#endif

#endif
