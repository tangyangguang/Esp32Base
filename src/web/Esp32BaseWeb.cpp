#include "../Esp32BaseProfile.h"

#if ESP32BASE_ENABLE_WEB

#include "internal/WebInternal.h"

using namespace esp32base_web;

bool Esp32BaseWeb::begin() {
    if (g_webReady) {
        return true;
    }
    const uint8_t appRoutes = routeCount(false);
    const uint8_t appPages = routeCount(true);
    uint8_t builtinRoutes = 26;
#if ESP32BASE_ENABLE_FS
    builtinRoutes += 3;
#endif
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
    g_footerBarMode = readFooterBarMode();
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
    g_server.on("/esp32base/api/wifi/retry", HTTP_POST, handleWifiRetry);
    g_server.on("/esp32base/api/wifi/clear", HTTP_POST, handleWifiClear);
    g_server.on("/esp32base/logs", HTTP_GET, handleLogsPage);
    g_server.on("/esp32base/logs/raw", HTTP_GET, handleLogsRaw);
    g_server.on("/esp32base/logs/clear", HTTP_POST, handleLogsClear);
#if ESP32BASE_ENABLE_FS
    g_server.on("/esp32base/fs", HTTP_GET, handleFsPage);
    g_server.on("/esp32base/fs/check", HTTP_GET, handleFsCheckGet);
    g_server.on("/esp32base/fs/download", HTTP_GET, handleFsDownloadGet);
    g_server.on("/esp32base/fs/delete", HTTP_POST, handleFsDeletePost);
    g_server.on("/esp32base/fs/upload", HTTP_POST, handleFsUploadDone, handleFsUpload);
#endif
    g_server.on("/esp32base/auth", HTTP_GET, handleAuthPage);
    g_server.on("/esp32base/auth", HTTP_POST, handleAuthSubmit);
    g_server.on("/esp32base/tools", HTTP_GET, handleToolsPage);
#if ESP32BASE_ENABLE_APP_CONFIG
    g_server.on("/esp32base/app-config", HTTP_GET, handleAppConfigPage);
    g_server.on("/esp32base/app-config", HTTP_POST, handleAppConfigSubmit);
#endif
    g_server.on("/esp32base/tools/hostname", HTTP_POST, handleHostnameSubmit);
    g_server.on("/esp32base/tools/filelog", HTTP_POST, handleToolsFileLogPost);
    g_server.on("/esp32base/tools/footer-bar", HTTP_POST, handleToolsFooterBarPost);
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
    g_server.on("/esp32base/ui.css", HTTP_GET, handleUiCss);
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

bool Esp32BaseWeb::setFooterBarMode(FooterBarMode mode) {
    if (!validFooterBarMode(mode)) {
        return false;
    }
    const FooterBarMode previousMode = g_footerBarMode;
    g_footerBarMode = mode;
    const bool saved = Esp32BaseConfig::setInt("eb_ui", "footer_mode", static_cast<int32_t>(mode));
    ESP32BASE_LOG_W("web", "footer_bar_mode_%s from=%s to=%s saved=%s",
                    previousMode == mode ? "unchanged" : "changed",
                    ::footerBarModeName(previousMode),
                    ::footerBarModeName(mode),
                    saved ? "success" : "failed");
    return saved;
}

Esp32BaseWeb::FooterBarMode Esp32BaseWeb::footerBarMode() {
    return g_footerBarMode;
}

const char* Esp32BaseWeb::footerBarModeName() {
    return ::footerBarModeName(g_footerBarMode);
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
    if (shouldSendHeadExtra()) {
        g_headExtraCallback();
    }
    sendChunk("</head><body>");
    sendMainNav();
    sendChunk("<main class='page'>");
}

void Esp32BaseWeb::sendFooter() {
    sendChunk("</main>");
    if (g_systemNavMode != Esp32BaseWeb::SYSTEM_NAV_SECTION) {
        sendSystemNavSection();
    }
    switch (g_footerBarMode) {
        case Esp32BaseWeb::FOOTER_BAR_OFF:
            sendChunk("</body></html>");
            break;
        case Esp32BaseWeb::FOOTER_BAR_STATUS_ONLY:
            sendChunk("<footer class='footerbar'><span class='heap'>");
            sendFooterStats();
            sendChunk("</span></footer></body></html>");
            break;
        case Esp32BaseWeb::FOOTER_BAR_FULL:
        default:
            if (g_systemNavMode == Esp32BaseWeb::SYSTEM_NAV_SECTION) {
                sendChunk("<footer class='footerbar'><span class='syslinks'>");
                sendSystemLinks(false, activeNavPath(true));
                sendChunk("</span><span class='heap'>");
                sendFooterStats();
                sendChunk("</span></footer></body></html>");
            } else {
                sendChunk("<div class=info>");
                sendFooterStats();
                sendChunk("</div></body></html>");
            }
            break;
    }
    endResponse();
}

void Esp32BaseWeb::sendPageTitle(const char* title, const char* subtitle) {
    sendChunk("<header class='pagehead'><h1>");
    writeHtmlEscaped(title ? title : "");
    sendChunk("</h1>");
    if (subtitle && subtitle[0]) {
        sendChunk("<p>");
        writeHtmlEscaped(subtitle);
        sendChunk("</p>");
    }
    sendChunk("</header>");
}

void Esp32BaseWeb::beginPanel(const char* title) {
    sendChunk("<section class='panel'>");
    if (title && title[0]) {
        sendChunk("<h2>");
        writeHtmlEscaped(title);
        sendChunk("</h2>");
    }
}

void Esp32BaseWeb::endPanel() {
    sendChunk("</section>");
}

void Esp32BaseWeb::sendNotice(UiTone tone, const char* title, const char* message) {
    sendChunk("<div class='notice");
    sendChunk(uiToneClass(tone));
    sendChunk("'><b>");
    writeHtmlEscaped(title ? title : "");
    sendChunk("</b>");
    if (message && message[0]) {
        sendChunk("<br>");
        writeHtmlEscaped(message);
    }
    sendChunk("</div>");
}

void Esp32BaseWeb::sendResultNotice(const ResultNotice* notices, uint8_t count) {
    if (!notices || count == 0) {
        return;
    }
    char value[32];
    for (uint8_t i = 0; i < count; ++i) {
        const ResultNotice& notice = notices[i];
        if (!notice.param || !notice.param[0]) {
            continue;
        }
        if (!hasParam(notice.param)) {
            continue;
        }
        if (notice.value && notice.value[0]) {
            if (!getParam(notice.param, value, sizeof(value)) || strcmp(value, notice.value) != 0) {
                continue;
            }
        }
        sendNotice(notice.tone, notice.title, notice.message);
        return;
    }
}

void Esp32BaseWeb::beginMetricGrid() {
    sendChunk("<div class='metrics'>");
}

void Esp32BaseWeb::sendMetric(const char* label, const char* value, const char* help) {
    sendChunk("<div class='metric'><b>");
    writeHtmlEscaped(value ? value : "");
    sendChunk("</b><span>");
    writeHtmlEscaped(label ? label : "");
    if (help && help[0]) {
        sendChunk(" · ");
        writeHtmlEscaped(help);
    }
    sendChunk("</span></div>");
}

void Esp32BaseWeb::endMetricGrid() {
    sendChunk("</div>");
}

static bool sendInfoRowCompactStart(const char* title, const char* help, const char* value, bool hasAction) {
    sendChunk("<div class='urow'><div><b>");
    sendEscapedHtmlChunk(title ? title : "");
    sendChunk("</b>");
    if (help && help[0]) {
        sendChunk("<small>");
        sendEscapedHtmlChunk(help);
        sendChunk("</small>");
    }
    sendChunk(hasAction ? "</div><div class='uactions'>" : "</div><div class='uactions readonly'>");
    const bool hasValue = value && value[0];
    if (hasValue) {
        sendChunk("<span class='uvalue'>");
        sendEscapedHtmlChunk(value);
        sendChunk("</span>");
    } else if (hasAction) {
        sendChunk("<span class='uvalue empty'></span>");
    }
    return hasValue;
}

static void sendInfoRowCompactEnd() {
    sendChunk("</div></div>");
}

static void sendInfoRowActionGap(bool hasValue) {
    (void)hasValue;
}

static void sendInfoRowActionLink(const char* href, const char* label, Esp32BaseWeb::UiTone tone) {
    sendChunk("<a class='btnlink");
    sendChunk(uiToneClass(tone));
    sendChunk("' href='");
    sendEscapedHtmlChunk(href ? href : "#");
    sendChunk("'>");
    sendEscapedHtmlChunk(label ? label : "");
    sendChunk("</a>");
}

void Esp32BaseWeb::sendInfoRowCompact(const char* title, const char* help, const char* value) {
    sendInfoRowCompactStart(title, help, value, false);
    sendInfoRowCompactEnd();
}

void Esp32BaseWeb::sendInfoRowCompactLink(const char* title, const char* help, const char* value,
                                          const char* href, const char* label, UiTone tone) {
    const bool hasValue = sendInfoRowCompactStart(title, help, value, true);
    sendInfoRowActionGap(hasValue);
    sendInfoRowActionLink(href, label, tone);
    sendInfoRowCompactEnd();
}

void Esp32BaseWeb::sendInfoRowCompactForm(const char* title, const char* help, const char* value,
                                          const char* action, const char* label,
                                          const char* hiddenName, const char* hiddenValue,
                                          UiTone tone) {
    const bool hasValue = sendInfoRowCompactStart(title, help, value, true);
    sendInfoRowActionGap(hasValue);
    sendChunk("<form method='post' action='");
    sendEscapedHtmlChunk(action ? action : "");
    sendChunk("' onsubmit='return once(this)'>");
    if (hiddenName && hiddenName[0]) {
        sendChunk("<input type='hidden' name='");
        sendEscapedHtmlChunk(hiddenName);
        sendChunk("' value='");
        sendEscapedHtmlChunk(hiddenValue ? hiddenValue : "");
        sendChunk("'>");
    }
    sendChunk("<input type='submit' class='btnlink");
    sendChunk(uiToneClass(tone));
    sendChunk("'");
    sendChunk(" value='");
    sendEscapedHtmlChunk(label ? label : "");
    sendChunk("'></form>");
    sendInfoRowCompactEnd();
}

void Esp32BaseWeb::sendPagination(const Pagination& pagination) {
    const uint32_t perPage = pagination.perPage == 0 ? 20 : pagination.perPage;
    const uint32_t totalPages = pagination.total == 0 ? 1 : ((pagination.total + perPage - 1) / perPage);
    uint32_t page = pagination.page == 0 ? 1 : pagination.page;
    if (page > totalPages) {
        page = totalPages;
    }
    char left[64];
    snprintf(left, sizeof(left), "共 %lu 条 / %lu 页",
             static_cast<unsigned long>(pagination.total),
             static_cast<unsigned long>(totalPages));
    sendChunk("<div class='pagination'><span class='muted'>");
    writeHtmlEscaped(left);
    sendChunk("</span><span class='pagerlinks'>");
    sendPaginationLink("首页", pagination, 1, page <= 1);
    sendPaginationLink("上一页", pagination, page > 1 ? page - 1 : 1, page <= 1);
    uint32_t startPage = page > 2 ? page - 2 : 1;
    uint32_t endPage = startPage + 4;
    if (endPage > totalPages) {
        endPage = totalPages;
    }
    if (endPage >= 4 && endPage - startPage < 4) {
        startPage = endPage > 4 ? endPage - 4 : 1;
    }
    for (uint32_t i = startPage; i <= endPage; ++i) {
        sendPaginationPageNumber(pagination, i, page);
    }
    sendPaginationLink("下一页", pagination, page < totalPages ? page + 1 : totalPages, page >= totalPages);
    sendPaginationLink("尾页", pagination, totalPages, page >= totalPages);
    sendChunk("</span><form method='get' class='pagerform'");
    sendPaginationActionPath(pagination.path);
    sendChunk(">");
    const char* pathQuery = pagination.path ? strchr(pagination.path, '?') : nullptr;
    sendQueryHiddenInputs(pathQuery ? pathQuery + 1 : nullptr);
    sendQueryHiddenInputs(pagination.query);
    sendChunk("<label class='muted'>每页</label><select name='per'>");
    const uint16_t options[] = {10, 20, 50};
    for (uint8_t i = 0; i < 3; ++i) {
        char opt[8];
        snprintf(opt, sizeof(opt), "%u", static_cast<unsigned>(options[i]));
        sendChunk("<option value='");
        sendChunk(opt);
        if (perPage == options[i]) {
            sendChunk("' selected>");
        } else {
            sendChunk("'>");
        }
        sendChunk(opt);
        sendChunk("</option>");
    }
    char pageValue[16];
    char totalValue[16];
    snprintf(pageValue, sizeof(pageValue), "%lu", static_cast<unsigned long>(page));
    snprintf(totalValue, sizeof(totalValue), "%lu", static_cast<unsigned long>(totalPages));
    sendChunk("</select><label class='muted'>跳至</label><input type='number' name='page' min='1' max='");
    sendChunk(totalValue);
    sendChunk("' value='");
    sendChunk(pageValue);
    sendChunk("'><button type='submit'>跳转</button></form></div>");
}

bool Esp32BaseWeb::sendResponseHeader(const char* name, const char* value) {
    return ::sendResponseHeader(name, value);
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

#endif
