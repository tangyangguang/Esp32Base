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

#if ESP32BASE_ENABLE_BUS
#include "../runtime/Esp32BaseBus.h"
#endif
#if ESP32BASE_ENABLE_OTA
#include "../update/Esp32BaseOta.h"
#endif

#include <WebServer.h>
#include <Preferences.h>
#include <mbedtls/base64.h>
#include <stdlib.h>
#include <string.h>

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
const char* g_headerKeys[] = {"Authorization", "X-Sha256", "X-Firmware-Size"};
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
Esp32BaseWeb::SystemNavMode g_systemNavMode = Esp32BaseWeb::SYSTEM_NAV_TOP;
char g_builtinLabels[7][16] = {"Status", "WiFi", "OTA", "Logs", "Tools", "Tools", "Auth"};
Esp32BaseWeb::Handler g_headExtraCallback = nullptr;
char g_chunkBuffer[512];
size_t g_chunkUsed = 0;
char g_activeUri[48] = "";
Esp32BaseWeb::Method g_currentMethod = Esp32BaseWeb::METHOD_UNKNOWN;
Esp32BaseWeb::Method g_lastRequestMethod = Esp32BaseWeb::METHOD_UNKNOWN;
bool g_requestContextActive = false;
bool g_responseActive = false;
bool g_authLoggedForRequest = false;

static const char WEB_HEAD[] PROGMEM =
    "<meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<style>"
    "body{font-family:sans-serif;font-size:14px;line-height:1.45;padding:12px;max-width:760px;margin:auto;color:#24303d}"
    "h1,h2,h3{margin:0 0 12px}h1{font-size:20px}h2{font-size:18px}h3{font-size:16px}"
    "a{color:#24303d;text-decoration:none}button,input[type=submit],input[type=button]{background:#24303d;color:#fff;padding:7px 12px;border:0;border-radius:3px;cursor:pointer;margin:2px 2px 2px 0}"
    "input:not([type]),input[type=text],input[type=password],input[type=number],input[type=email],input[type=url],input[type=tel],input[type=search]{width:100%;padding:7px;margin:4px 0 12px;border:1px solid #d9e0e6;border-radius:3px;box-sizing:border-box}"
    "nav{margin-bottom:14px;border-bottom:1px solid #d9e0e6;padding-bottom:8px}"
    "details{margin:14px 0}summary{cursor:pointer;color:#24303d}"
    "nav a{font-size:16px;display:inline-block;padding:4px 8px;margin:2px 2px 2px 0;border-radius:6px;color:#24303d}.brand{font-weight:600}nav a.active{background:#e7f1ec;color:#356b5f;font-weight:600}.ok{color:green}.err{color:#c33}.info{color:#667085;font-size:.86em;margin-top:16px}"
    ".footerbar{margin-top:18px;padding-top:8px;border-top:1px solid #d9e0e6;display:flex;flex-wrap:wrap;align-items:center;justify-content:space-between;gap:12px;color:#667085;font-size:13px}"
    ".syslinks{min-width:0;flex:1 1 auto;display:flex;flex-wrap:wrap;gap:4px;align-items:center}"
    ".syslinks a{font-size:1em;line-height:1.35;padding:3px 7px;margin:0;background:#f4f6f5;color:#667085;border-radius:4px}"
    ".heap{flex:0 0 auto;margin-left:auto;white-space:nowrap;text-align:right;font-size:1em}"
    "@media(max-width:520px){.footerbar{gap:6px 10px;font-size:.76em;align-items:flex-start}.syslinks a{padding:3px 6px}}"
    "@media(max-width:360px){.heap{margin-left:0;text-align:left}}"
    ".kv{border-collapse:collapse;width:100%;margin:8px 0 14px}.kv th,.kv td{border-bottom:1px solid #d9e0e6;padding:6px 4px;text-align:left;vertical-align:top}.kv th{width:34%;color:#667085;font-weight:400}"
    ".tabs{display:flex;flex-wrap:wrap;gap:4px;margin:8px 0 10px}.tabs a{font-size:14px;padding:4px 8px;margin:0;background:#f4f6f5;color:#667085;border-radius:4px}.tabs a.active{background:#e7f1ec;color:#356b5f;font-weight:600}"
    ".toolsection{margin:18px 0 0;padding-top:16px;border-top:1px solid #d9e0e6}.toolsection:first-of-type{margin-top:6px;padding-top:0;border-top:0}.toolsection form{margin:10px 0 0}"
    ".logmeta{line-height:1.45}"
    "pre{white-space:pre;overflow-x:auto;word-break:normal;font-size:.82em;background:#f7f7f7;padding:8px}"
    "</style>"
    "<script>function once(f){if(f.dataset.busy)return false;f.dataset.busy=1;var b=f.querySelector('[type=submit]');if(b)b.disabled=true;return true;}</script>";

void flushChunkBuffer();
void markRequest();
void sendChunk(const char* text);
void sendEscapedHtmlChunk(const char* text);

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
    if (!header.startsWith("Basic ") || header.length() > 180) {
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
        if (ok) {
            ESP32BASE_LOG_D("web", "auth_request context=%s user=%s password=%s result=success",
                            context ? context : "unknown",
                            user,
                            pass);
        } else {
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

#if ESP32BASE_ENABLE_OTA
const char* otaStatusName(Esp32BaseOta::Status status) {
    switch (status) {
        case Esp32BaseOta::IDLE: return "IDLE";
        case Esp32BaseOta::READY: return "READY";
        case Esp32BaseOta::UPLOADING: return "UPLOADING";
        case Esp32BaseOta::VERIFYING: return "VERIFYING";
        case Esp32BaseOta::SUCCESS: return "SUCCESS";
        case Esp32BaseOta::FAILED: return "FAILED";
        default: return "UNKNOWN";
    }
}
#endif

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
        if (strcmp(currentPath, "/esp32base/reboot") == 0) {
            activePath = "/esp32base/tools";
            activeLen = strlen(activePath);
        }
        updateActivePath("/esp32base", currentPath, activePath, activeLen);
        updateActivePath("/esp32base/wifi", currentPath, activePath, activeLen);
        updateActivePath("/esp32base/auth", currentPath, activePath, activeLen);
#if ESP32BASE_ENABLE_OTA
        updateActivePath("/esp32base/ota", currentPath, activePath, activeLen);
#endif
        updateActivePath("/esp32base/logs", currentPath, activePath, activeLen);
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
    sendNavLink("/esp32base/wifi", g_builtinLabels[Esp32BaseWeb::BUILTIN_WIFI], paragraph, activePath);
    sendNavLink("/esp32base/auth", g_builtinLabels[Esp32BaseWeb::BUILTIN_AUTH], paragraph, activePath);
#if ESP32BASE_ENABLE_OTA
    sendNavLink("/esp32base/ota", g_builtinLabels[Esp32BaseWeb::BUILTIN_OTA], paragraph, activePath);
#endif
    sendNavLink("/esp32base/logs", g_builtinLabels[Esp32BaseWeb::BUILTIN_LOGS], paragraph, activePath);
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

void sendProgmem(const char* p) {
    flushChunkBuffer();
    char buf[129];
    size_t used = 0;
    while (true) {
        const char c = static_cast<char>(pgm_read_byte(p++));
        if (!c) {
            break;
        }
        buf[used++] = c;
        if (used == sizeof(buf) - 1) {
            buf[used] = '\0';
            g_server.sendContent(buf);
            used = 0;
        }
    }
    if (used) {
        buf[used] = '\0';
        g_server.sendContent(buf);
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
        snprintf(disposition, sizeof(disposition), "attachment; filename=\"%s\"", filename);
        g_server.sendHeader("Content-Disposition", disposition);
    }
    g_chunkUsed = 0;
    g_responseActive = true;
    g_server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    g_server.send(code, contentType, "");
    return true;
}

void flushChunkBuffer() {
    if (g_chunkUsed == 0) {
        return;
    }
    g_chunkBuffer[g_chunkUsed] = '\0';
    g_server.sendContent(g_chunkBuffer);
    g_chunkUsed = 0;
}

void sendChunk(const char* text) {
    if (!text) {
        return;
    }
    if (!g_responseActive) {
        ESP32BASE_LOG_W("web", "send_chunk outside response");
        return;
    }
    while (*text) {
        const size_t space = sizeof(g_chunkBuffer) - 1U - g_chunkUsed;
        if (space == 0) {
            flushChunkBuffer();
            continue;
        }
        size_t len = strlen(text);
        if (len > space) {
            len = space;
        }
        memcpy(g_chunkBuffer + g_chunkUsed, text, len);
        g_chunkUsed += len;
        text += len;
    }
}

void endResponse() {
    if (!g_responseActive) {
        ESP32BASE_LOG_W("web", "end_response outside response");
        return;
    }
    flushChunkBuffer();
    g_server.sendContent("");
    g_responseActive = false;
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
    sendChunk("{\"freeHeap\":");
    sendBytesJsonChunk(Esp32BaseSystem::freeHeap());
    sendChunk(",\"minFreeHeap\":");
    sendBytesJsonChunk(Esp32BaseSystem::minFreeHeap());
    sendChunk(",\"totalHeap\":");
    sendBytesJsonChunk(Esp32BaseSystem::totalHeap());
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
        sendChunk("<p class=err>WiFi settings were not saved.</p>");
    }
    sendChunk("<h2>WiFi Settings</h2><form method='post' action='/esp32base/wifi' onsubmit=\"if(!this.ssid.value.trim()){alert('SSID cannot be empty');return false;}if(!this.password.value.trim()){alert('Password cannot be empty');return false;}if(this.ssid.value.length>32||this.password.value.length>64){alert('SSID or password is too long');return false;}return once(this);\">");
    sendChunk("SSID<input name='ssid' value='");
    sendEscapedHtmlChunk(ssid);
    sendChunk("' maxlength='32' autocomplete='off'>Password<input id='wp' type='password' name='password' value='");
    sendEscapedHtmlChunk(password);
    sendChunk("' maxlength='64'><input type='button' value='Show/Hide Password' onclick=\"var p=document.getElementById('wp');p.type=p.type=='password'?'text':'password'\"><input type='submit' value='Save &amp; Connect'></form>");
    sendChunk("<form method='post' action='/esp32base/api/wifi/clear' onsubmit=\"return confirm('Clear WiFi credentials?')&&once(this)\"><input type='submit' value='Clear WiFi' style='background:#c33'></form>");
    Esp32BaseWeb::sendFooter();
}

void handleWifiSubmit() {
    markRequest();
    if (!ensureAuth()) {
        return;
    }
    const String ssid = g_server.arg("ssid");
    const String password = g_server.arg("password");
    if (ssid.length() == 0 || password.length() == 0 || ssid.length() > 32 || password.length() > 64) {
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
    if (!ensureAuth()) {
        return;
    }
    Esp32BaseWiFi::clearCredentials();
    redirectSeeOther("/esp32base/wifi?cleared=1");
}

void handleRestart() {
    markRequest();
    if (!ensureAuth()) {
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
    sendChunk("<h2>");
    sendEscapedHtmlChunk(g_builtinLabels[Esp32BaseWeb::BUILTIN_TOOLS]);
    sendChunk("</h2>");
    if (g_server.hasArg("formatted")) {
        sendChunk("<p class='ok'><b>LittleFS formatted.</b> FileLog configuration was reloaded.</p>");
    } else if (g_server.hasArg("error")) {
        sendChunk("<p class='err'>Tools action failed: ");
        sendEscapedHtmlChunk(g_server.arg("error").c_str());
        sendChunk("</p>");
    } else if (g_server.hasArg("restarting")) {
        sendChunk("<p class='ok'><b>Restart requested.</b> Device is restarting. Please wait a few seconds, then reload.</p>");
    }
    sendChunk("<section class='toolsection'><h3>Restart device</h3><p>Restart the device through the normal lifecycle path.</p><form method='post' action='/esp32base/tools/reboot' onsubmit=\"return confirm('Reboot device now?')&&once(this)\"><input type='submit' value='Restart device' style='background:#c33'></form></section>");
#if ESP32BASE_ENABLE_FS
    sendChunk("<section class='toolsection'><h3>Format LittleFS</h3><p class='err'>This deletes logs and all files stored in LittleFS. WiFi, Web Auth and NVS config are not cleared.</p><form method='post' action='/esp32base/tools/format-fs' onsubmit=\"return confirm('Format LittleFS? This deletes logs and all files stored in LittleFS.')&&once(this)\"><input type='submit' value='Format LittleFS' style='background:#c33'></form></section>");
#else
    sendChunk("<section class='toolsection'><h3>Format LittleFS</h3><p>LittleFS is unavailable in this profile.</p></section>");
#endif
    Esp32BaseWeb::sendFooter();
}

void handleToolsRebootPost() {
    markRequest();
    if (!ensureAuth()) {
        return;
    }
    ESP32BASE_LOG_W("web", "restart_requested source=tools");
    Esp32BaseWeb::sendHeader("Rebooting");
    sendChunk("<script>history.replaceState(null,'','/esp32base/tools?restarting=1');</script><h2>Rebooting...</h2><p>Device is restarting. Please wait a few seconds, then reload.</p>");
    Esp32BaseWeb::sendFooter();
    delay(100);
    Esp32BaseSystem::restart("web");
}

void handleToolsFormatFsPost() {
    markRequest();
    if (!ensureAuth()) {
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
    sendChunk("<h2>OTA Update</h2><form id='f'><input id='fw' type='file' name='firmware' accept='.bin' required><input id='sha' placeholder='SHA256 optional' maxlength='64'><input type='submit' value='Upload Firmware'></form><progress id='p' value='0' max='100' style='width:100%;display:none'></progress><p id='s'></p>");
    sendChunk("<script>function h(n){var u=['B','KB','MB','GB'],i=0,x=n;while(x>=1024&&i<u.length-1){x/=1024;i++;}return n+' bytes ('+(i?x.toFixed(2):x)+' '+u[i]+')';}document.getElementById('f').onsubmit=function(e){e.preventDefault();if(this.dataset.busy)return false;this.dataset.busy=1;var f=document.getElementById('fw').files[0],st=document.getElementById('s'),pg=document.getElementById('p'),b=this.querySelector('[type=submit]');if(!f){this.dataset.busy='';return false;}var d=new FormData();d.append('firmware',f,f.name);var x=new XMLHttpRequest();if(b)b.disabled=true;pg.style.display='block';st.textContent='Uploading 0%';x.upload.onprogress=function(ev){if(ev.lengthComputable){var p=Math.floor(ev.loaded*100/ev.total);pg.value=p;st.textContent='Uploading '+p+'% '+h(ev.loaded)+' / '+h(ev.total);}};x.onload=function(){var r={};try{r=JSON.parse(x.responseText||'{}');}catch(e){}if(x.status==200){st.textContent='Upload successful. Restarting...';setTimeout(function(){location.href=r.redirect||'/esp32base';},8000);return;}st.textContent='Upload failed: '+(r.error||('HTTP '+x.status));document.getElementById('f').dataset.busy='';if(b)b.disabled=false;};x.onerror=function(){st.textContent='Upload failed: network error';document.getElementById('f').dataset.busy='';if(b)b.disabled=false;};x.open('POST','/esp32base/ota');x.setRequestHeader('X-Firmware-Size',String(f.size));var s=document.getElementById('sha').value;if(s)x.setRequestHeader('X-Sha256',s);x.send(d);return false;};</script>");
    Esp32BaseWeb::sendFooter();
}

void handleOtaUploadDone() {
    markRequest();
    if (!ensureAuth()) {
        return;
    }
    const bool ok = Esp32BaseOta::status() == Esp32BaseOta::SUCCESS;
    if (ok) {
        if (beginResponse(200, "application/json", nullptr)) {
            sendChunk("{\"ok\":true,\"redirect\":\"");
            sendEscapedJsonChunk(configuredHomePath());
            sendChunk("\"}");
            endResponse();
        }
    } else {
        if (beginResponse(500, "application/json", nullptr)) {
            sendChunk("{\"ok\":false,\"error\":\"");
            sendEscapedJsonChunk(Esp32BaseOta::lastError());
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
        Esp32BaseOta::startUpload(firmwareSize, sha256[0] ? sha256 : nullptr);
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        Esp32BaseOta::writeChunk(upload.buf, upload.currentSize);
    } else if (upload.status == UPLOAD_FILE_END) {
        Esp32BaseOta::finishUpload();
    } else if (upload.status == UPLOAD_FILE_ABORTED) {
        Esp32BaseOta::abortUpload("client aborted");
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
    sendChunk("<div class='statuspage'><h2>");
    sendEscapedHtmlChunk(g_deviceName);
    sendChunk("</h2><table class='kv'>");
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
    char value[96];
    Esp32BaseLog::formatUptime(Esp32BaseSystem::uptimeMs(), value, sizeof(value));
    sendInfoRow("Uptime", value);
    snprintf(value, sizeof(value), "%lu", static_cast<unsigned long>(Esp32BaseSystem::bootCount()));
    sendInfoRow("Boot count", value);
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
    char freeHeap[48];
    char minHeap[48];
    char totalHeap[48];
    char flash[48];
    formatReadableBytes(Esp32BaseSystem::freeHeap(), freeHeap, sizeof(freeHeap));
    formatReadableBytes(Esp32BaseSystem::minFreeHeap(), minHeap, sizeof(minHeap));
    formatReadableBytes(Esp32BaseSystem::totalHeap(), totalHeap, sizeof(totalHeap));
    formatReadableBytes(Esp32BaseSystem::flashSize(), flash, sizeof(flash));
    snprintf(value, sizeof(value), "free %s, min %s, total %s", freeHeap, minHeap, totalHeap);
    sendInfoRow("Heap", value);
    sendInfoRow("Flash", flash);
#if ESP32BASE_ENABLE_WIFI
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
        snprintf(value, sizeof(value), "<br>RSSI %ld dBm / power save %s",
                 static_cast<long>(Esp32BaseWiFi::rssi()), Esp32BaseWiFi::powerSave() ? "on" : "off");
        sendChunk(value);
    }
    sendInfoRowEnd();
#endif
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
        sendEscapedHtmlChunk(Esp32BaseFileLog::levelName());
        sendChunk(" / current ");
        sendEscapedHtmlChunk(sizeBuf);
    }
    sendInfoRowEnd();
#endif
#if ESP32BASE_ENABLE_NTP
    const uint32_t epoch = Esp32BaseNtp::timestamp();
    const bool synced = epoch >= ESP32BASE_NTP_SYNC_MIN_EPOCH;
    sendInfoRowStart("Time");
    sendEscapedHtmlChunk(Esp32BaseNtp::isStarted() ? (synced ? "synced" : "pending") : "not started");
    if (synced && Esp32BaseNtp::formatTime(value, sizeof(value), "%Y-%m-%d %H:%M:%S")) {
        sendChunk("<br>");
        sendEscapedHtmlChunk(value);
    }
    sendInfoRowEnd();
#endif
#if ESP32BASE_ENABLE_OTA
    sendInfoRow("OTA", otaStatusName(Esp32BaseOta::status()));
#endif
    sendChunk("</table>");
    if (g_homeMode == Esp32BaseWeb::HOME_ESP32BASE && appNavCount() > 0) {
        sendChunk("<h2>Application</h2>");
        sendAppLinks(true, nullptr);
    }
    sendChunk("</div>");
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

void sendLogEscapedChunk(const char* data, size_t len, void* user) {
    (void)user;
    char one[2] = {0, 0};
    for (size_t i = 0; i < len; ++i) {
        switch (data[i]) {
            case '&': sendChunk("&amp;"); break;
            case '<': sendChunk("&lt;"); break;
            case '>': sendChunk("&gt;"); break;
            case '"': sendChunk("&quot;"); break;
            default:
                one[0] = data[i];
                sendChunk(one);
                break;
        }
    }
}

void sendLogSegment(uint8_t index, const char* title) {
    sendChunk("\n\n----- ");
    sendEscapedHtmlChunk(title);
    sendChunk(" -----\n");
    Esp32BaseFileLog::streamSegment(index, sendLogEscapedChunk);
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
        sendEscapedHtmlChunk(title);
        sendChunk(" ");
        sendEscapedHtmlChunk(sizeBuf);
        sendChunk("</a>");
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
    sendChunk("<h2>");
    sendEscapedHtmlChunk(g_builtinLabels[Esp32BaseWeb::BUILTIN_LOGS]);
    sendChunk("</h2>");
#if ESP32BASE_ENABLE_FILELOG
    sendChunk("<form method='post' action='/esp32base/logs/clear' onsubmit=\"return confirm('Clear log files?')&&once(this)\"><input type='submit' value='Clear Log' style='background:#c33'></form>");
    if (!Esp32BaseFileLog::isEnabled()) {
        sendChunk("<p>File log: <b>disabled</b></p>");
        Esp32BaseWeb::sendFooter();
        return;
    }
    Esp32BaseFileLog::flush();
    char maxBuf[48];
    char totalBuf[48];
    char bufferUsed[48];
    char bufferTotal[48];
    formatReadableBytes(Esp32BaseFileLog::maxBytes(), maxBuf, sizeof(maxBuf));
    formatReadableBytes(static_cast<uint64_t>(Esp32BaseFileLog::maxBytes()) * Esp32BaseFileLog::rotateFiles(), totalBuf, sizeof(totalBuf));
    formatReadableBytes(Esp32BaseFileLog::bufferUsed(), bufferUsed, sizeof(bufferUsed));
    formatReadableBytes(Esp32BaseFileLog::bufferSize(), bufferTotal, sizeof(bufferTotal));
    char line[192];
    snprintf(line, sizeof(line), "<p class='logmeta'>File log: <b>enabled</b><br>Path: ");
    sendChunk(line);
    sendEscapedHtmlChunk(Esp32BaseFileLog::path());
    snprintf(line, sizeof(line), "<br>Rotation files: %u<br>File level: %s<br>Buffer: %s / %s<br>Flush interval: %lu ms<br>Max per file: %s<br>Max total: %s<br>Segments: ",
             static_cast<unsigned>(Esp32BaseFileLog::rotateFiles()),
             Esp32BaseFileLog::levelName(),
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
    sendChunk("</p>");
    sendLogSegmentTabs(selectedSegment);
    sendChunk("<pre>");
    char selectedTitle[16];
    logSegmentTitle(selectedSegment, selectedTitle, sizeof(selectedTitle));
    sendLogSegment(selectedSegment, selectedTitle);
    sendChunk("</pre>");
#else
    sendChunk("<p>File log: <b>unavailable</b></p>");
#endif
    Esp32BaseWeb::sendFooter();
}

void handleLogsClear() {
    markRequest();
    if (!ensureAuth()) {
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
    sendChunk("<h2>");
    sendEscapedHtmlChunk(g_builtinLabels[Esp32BaseWeb::BUILTIN_AUTH]);
    sendChunk("</h2>");
    if (g_server.hasArg("saved")) {
        sendChunk("<p class='ok'>Auth updated. Use the new credentials on the next auth prompt.</p>");
    } else if (g_server.hasArg("error")) {
        sendChunk("<p class='err'>Auth update failed: ");
        sendEscapedHtmlChunk(g_server.arg("error").c_str());
        sendChunk("</p>");
    }
    sendChunk("<form method='post' action='/esp32base/auth' onsubmit=\"var f=this;function v(n){return f.elements[n].value.trim();}if(!v('current_user')||!v('current_pass')||!v('new_user')||!v('new_pass')||!v('confirm_pass')){alert('All auth fields are required');return false;}if(v('new_pass')!=v('confirm_pass')){alert('New passwords do not match');return false;}return once(this)\">");
    sendChunk("Current auth user<input name='current_user' maxlength='31' required value='");
    sendEscapedHtmlChunk(g_authUser);
    sendChunk("'>");
    sendChunk("Current auth password<input type='password' name='current_pass' maxlength='63' autocomplete='current-password' required>");
    sendChunk("New auth user<input name='new_user' maxlength='31' required value='");
    sendEscapedHtmlChunk(g_authUser);
    sendChunk("'>");
    sendChunk("New auth password<input type='password' name='new_pass' maxlength='63' autocomplete='new-password' required>");
    sendChunk("Confirm new auth password<input type='password' name='confirm_pass' maxlength='63' autocomplete='new-password' required>");
    sendChunk("<input type='submit' value='Save Auth'></form>");
    Esp32BaseWeb::sendFooter();
}

void handleAuthSubmit() {
    markRequest();
    if (!ensureAuth()) {
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
    uint8_t builtinRoutes = 20;
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
    g_server.on("/esp32base/wifi", HTTP_GET, handleWifiPage);
    g_server.on("/esp32base/wifi", HTTP_POST, handleWifiSubmit);
    g_server.on("/esp32base/api/wifi", HTTP_POST, handleWifiSubmit);
    g_server.on("/esp32base/api/wifi/clear", HTTP_POST, handleWifiClear);
    g_server.on("/esp32base/logs", HTTP_GET, handleLogsPage);
    g_server.on("/esp32base/logs/clear", HTTP_POST, handleLogsClear);
    g_server.on("/esp32base/auth", HTTP_GET, handleAuthPage);
    g_server.on("/esp32base/auth", HTTP_POST, handleAuthSubmit);
    g_server.on("/esp32base/tools", HTTP_GET, handleToolsPage);
    g_server.on("/esp32base/tools/reboot", HTTP_POST, handleToolsRebootPost);
    g_server.on("/esp32base/tools/format-fs", HTTP_POST, handleToolsFormatFsPost);
    g_server.on("/esp32base/reboot", HTTP_GET, handleToolsPage);
    g_server.on("/esp32base/reboot", HTTP_POST, handleToolsRebootPost);
    g_server.on("/esp32base/api/restart", HTTP_POST, handleRestart);
    g_server.on("/generate_204", HTTP_GET, handleCaptiveProbe);
    g_server.on("/hotspot-detect.html", HTTP_GET, handleCaptiveProbe);
    g_server.on("/ncsi.txt", HTTP_GET, handleCaptiveProbe);
    g_server.on("/favicon.ico", HTTP_GET, handleNoContent);
#if ESP32BASE_ENABLE_OTA
    g_server.on("/esp32base/ota", HTTP_GET, handleOtaPage);
    g_server.on("/esp32base/ota", HTTP_POST, handleOtaUploadDone, handleOtaUpload);
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
    if (g_headExtraCallback) {
        g_headExtraCallback();
    }
    sendChunk("</head><body>");
    sendMainNav();
}

void Esp32BaseWeb::sendFooter() {
    char heap[48];
    formatReadableBytes(Esp32BaseSystem::freeHeap(), heap, sizeof(heap));
    if (g_systemNavMode == Esp32BaseWeb::SYSTEM_NAV_SECTION) {
        sendChunk("<footer class='footerbar'><span class='syslinks'>");
        sendSystemLinks(false);
        sendChunk("</span><span class='heap'>Free heap: ");
        sendEscapedHtmlChunk(heap);
        sendChunk("</span></footer></body></html>");
    } else {
        sendSystemNavSection();
        sendChunk("<div class=info>Free heap: ");
        sendEscapedHtmlChunk(heap);
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
    flushChunkBuffer();
    if (len > 0) {
        g_server.sendContent(reinterpret_cast<const char*>(data), len);
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

#endif
