#include "../../Esp32BaseProfile.h"

#if ESP32BASE_ENABLE_WEB

#include "WebInternal.h"

namespace esp32base_web {

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

const char* uiToneClass(Esp32BaseWeb::UiTone tone) {
    switch (tone) {
        case Esp32BaseWeb::UI_OK: return " ok";
        case Esp32BaseWeb::UI_WARN: return " warn";
        case Esp32BaseWeb::UI_DANGER: return " danger";
        case Esp32BaseWeb::UI_INFO: return " info";
        case Esp32BaseWeb::UI_NEUTRAL:
        default: return "";
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

bool routeMatchesMethod(const Route& route, Esp32BaseWeb::Method method) {
    return route.method == Esp32BaseWeb::METHOD_ANY || route.method == method;
}

Route* findRoute(const char* path, Esp32BaseWeb::Method method) {
    if (!path || !path[0]) {
        return nullptr;
    }
    for (uint8_t i = 0; i < ESP32BASE_WEB_MAX_ROUTES; ++i) {
        if (g_routes[i].handler && strcmp(g_routes[i].path, path) == 0 && routeMatchesMethod(g_routes[i], method)) {
            return &g_routes[i];
        }
    }
    return nullptr;
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
        char lengthBuf[24];
        Esp32BaseLog::formatBytes(header.length(), lengthBuf, sizeof(lengthBuf));
        ESP32BASE_LOG_W("web", "auth_header_rejected reason=too_long length=%s", lengthBuf);
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

bool applyDefaultAuth() {
    if (g_defaultAuthSet) {
        applyPlainAuth(g_defaultAuthUser, g_defaultAuthPass);
        return true;
    }
#if ESP32BASE_WEB_ALLOW_INSECURE_DEFAULT_AUTH
    applyPlainAuth("admin", "admin");
    return true;
#else
    g_authUser[0] = '\0';
    g_authPass[0] = '\0';
    g_authLoadedFromStorage = false;
    return false;
#endif
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

bool validFooterBarMode(Esp32BaseWeb::FooterBarMode mode) {
    return mode == Esp32BaseWeb::FOOTER_BAR_OFF ||
           mode == Esp32BaseWeb::FOOTER_BAR_STATUS_ONLY ||
           mode == Esp32BaseWeb::FOOTER_BAR_FULL;
}

const char* footerBarModeName(Esp32BaseWeb::FooterBarMode mode) {
    switch (mode) {
        case Esp32BaseWeb::FOOTER_BAR_OFF: return "Off";
        case Esp32BaseWeb::FOOTER_BAR_STATUS_ONLY: return "Status only";
        case Esp32BaseWeb::FOOTER_BAR_FULL: return "Links + status";
        default: return "Unknown";
    }
}

Esp32BaseWeb::FooterBarMode readFooterBarMode() {
    const int32_t raw = Esp32BaseConfig::getInt("eb_ui", "footer_mode", Esp32BaseWeb::FOOTER_BAR_FULL);
    const Esp32BaseWeb::FooterBarMode mode = static_cast<Esp32BaseWeb::FooterBarMode>(raw);
    return validFooterBarMode(mode) ? mode : Esp32BaseWeb::FOOTER_BAR_FULL;
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

bool validHeaderName(const char* name, size_t maxLen) {
    if (!name || !name[0]) {
        return false;
    }
    size_t len = 0;
    for (const char* p = name; *p; ++p) {
        const char c = *p;
        const bool ok = (c >= 'A' && c <= 'Z') ||
                        (c >= 'a' && c <= 'z') ||
                        (c >= '0' && c <= '9') ||
                        c == '-';
        if (!ok) {
            return false;
        }
        ++len;
        if (len > maxLen) {
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
    if (!g_requestContextActive) {
        markRequest();
    }
    if (g_currentMethod != Esp32BaseWeb::METHOD_POST) {
        ESP32BASE_LOG_W("web",
                        "post_rejected context=%s reason=method method=%s",
                        context ? context : "unknown",
                        methodName(g_currentMethod));
        g_server.send(405, "text/plain", "Method Not Allowed");
        return false;
    }
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
        if (strcmp(g_navItems[i].path, "/index") == 0) {
            return g_navItems[i].path;
        }
    }
    for (uint8_t i = 0; i < ESP32BASE_WEB_MAX_ROUTES; ++i) {
        if (g_routes[i].handler && g_routes[i].appPage && strcmp(g_routes[i].path, "/index") == 0) {
            return g_routes[i].path;
        }
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

bool isBuiltinWebPath(const char* path) {
    static const char prefix[] = "/esp32base";
    if (!path || !path[0]) {
        return false;
    }
    const size_t len = sizeof(prefix) - 1;
    return strncmp(path, prefix, len) == 0 && (path[len] == '\0' || path[len] == '/');
}

bool shouldSendHeadExtra() {
    return g_headExtraCallback && !isBuiltinWebPath(g_activeUri);
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

void dispatchRoute(Route& route) {
    g_requestContextActive = true;
    markRequest();
    if (route.handler) {
        route.handler();
    }
    g_responseActive = false;
    g_requestContextActive = false;
    g_currentMethod = Esp32BaseWeb::METHOD_UNKNOWN;
}

void registerRoute(Route& route) {
    if (strcmp(route.path, "/") == 0 && routeMatchesMethod(route, Esp32BaseWeb::METHOD_GET)) {
        route.registered = true;
        return;
    }
    Route* routePtr = &route;
    g_server.on(route.path, toHttpMethod(route.method), [routePtr]() {
        dispatchRoute(*routePtr);
    });
    route.registered = true;
}

void handleCaptiveProbe() {
    g_server.sendHeader("Location", "/esp32base/wifi", true);
    g_server.send(302, "text/plain", "");
}

void handleRootRedirect() {
    markRequest();
    const char* location = g_homeMode == Esp32BaseWeb::HOME_ESP32BASE ? "/esp32base" : configuredHomePath();
    if (g_homeMode != Esp32BaseWeb::HOME_ESP32BASE && strcmp(configuredHomePath(), "/") == 0) {
        Route* route = findRoute("/", Esp32BaseWeb::METHOD_GET);
        if (route && route->handler) {
            dispatchRoute(*route);
            return;
        }
        location = "/esp32base";
    }
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

} // namespace esp32base_web

#endif
