#include "../Esp32BaseProfile.h"

#ifndef ESP32BASE_WEB_NATIVE_TEST
#define ESP32BASE_WEB_NATIVE_TEST 0
#endif

#include "Esp32BaseWeb.h"

#if ESP32BASE_ENABLE_RECORD_STORE
#include "../runtime/Esp32BaseRecordStore.h"
#include "../core/Esp32BaseLog.h"
#endif
#if ESP32BASE_ENABLE_BUS
#include "../runtime/Esp32BaseBus.h"
#endif

#include <stdio.h>
#include <string.h>

namespace {
Esp32BaseWeb::AfterFormatFsCallback g_afterFormatFsCallback = nullptr;
void* g_afterFormatFsCallbackUser = nullptr;
#if ESP32BASE_ENABLE_RECORD_STORE
Esp32BaseRecordStore* g_businessRecordStores[Esp32BaseWeb::MAX_BUSINESS_RECORD_STORES] = {};
uint8_t g_businessRecordStoreCount = 0;
#endif

void dispatchToolsFormatFsSuccess(bool mountSuccess,
                                  bool fileLogReloadSuccess,
                                  uint8_t businessRecordStoreCount,
                                  uint8_t businessRecordStoreReloadedCount,
                                  bool publishEvent) {
    Esp32BaseWeb::FormatFsResult result = {
        "tools",
        true,
        mountSuccess,
        fileLogReloadSuccess,
        businessRecordStoreCount,
        businessRecordStoreReloadedCount,
        businessRecordStoreCount == businessRecordStoreReloadedCount
    };
    if (g_afterFormatFsCallback) {
        g_afterFormatFsCallback(result, g_afterFormatFsCallbackUser);
    }
#if ESP32BASE_ENABLE_BUS
    if (publishEvent) {
        char data[240];
        snprintf(data,
                 sizeof(data),
                 "{\"source\":\"tools\",\"formatSuccess\":true,\"mountSuccess\":%s,\"fileLogReloadSuccess\":%s,\"businessRecordStoreCount\":%u,\"businessRecordStoreReloadedCount\":%u,\"businessRecordStoresReloadSuccess\":%s}",
                 mountSuccess ? "true" : "false",
                 fileLogReloadSuccess ? "true" : "false",
                 static_cast<unsigned>(businessRecordStoreCount),
                 static_cast<unsigned>(businessRecordStoreReloadedCount),
                 businessRecordStoreCount == businessRecordStoreReloadedCount ? "true" : "false");
        Esp32BaseBus::publish(Esp32BaseWeb::EVENT_TOOLS_FORMAT_FS_SUCCESS, data);
    }
#else
    (void)publishEvent;
#endif
}
}

bool Esp32BaseWeb::registerBusinessRecordStore(Esp32BaseRecordStore& store) {
#if ESP32BASE_ENABLE_RECORD_STORE
    Esp32BaseRecordStore::StoreStatus status;
    if (!store.readStatus(status)) {
        ESP32BASE_LOG_W("web", "business_record_store_registration_rejected reason=not_initialized");
        return false;
    }
    for (uint8_t i = 0; i < g_businessRecordStoreCount; ++i) {
        if (g_businessRecordStores[i] == &store) {
            return true;
        }
        Esp32BaseRecordStore::StoreStatus registeredStatus;
        if (g_businessRecordStores[i] && g_businessRecordStores[i]->readStatus(registeredStatus) &&
            registeredStatus.path && status.path && strcmp(registeredStatus.path, status.path) == 0) {
            ESP32BASE_LOG_W("web", "business_record_store_registration_rejected reason=duplicate_path path=%s",
                            status.path);
            return false;
        }
    }
    if (g_businessRecordStoreCount >= MAX_BUSINESS_RECORD_STORES) {
        ESP32BASE_LOG_W("web", "business_record_store_registration_rejected reason=capacity path=%s",
                        status.path ? status.path : "-");
        return false;
    }
    g_businessRecordStores[g_businessRecordStoreCount++] = &store;
    ESP32BASE_LOG_I("web", "business_record_store_registered path=%s count=%u",
                    status.path ? status.path : "-",
                    static_cast<unsigned>(g_businessRecordStoreCount));
    return true;
#else
    (void)store;
    return false;
#endif
}

void Esp32BaseWeb::setAfterFormatFsCallback(AfterFormatFsCallback cb, void* user) {
    g_afterFormatFsCallback = cb;
    g_afterFormatFsCallbackUser = cb ? user : nullptr;
}

void Esp32BaseWeb::clearAfterFormatFsCallback() {
    setAfterFormatFsCallback(nullptr, nullptr);
}

#if ESP32BASE_WEB_NATIVE_TEST

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

namespace {

struct NativeTestParam {
    std::string name;
    std::string value;
};

struct NativeTestRoute {
    std::string path;
    std::string title;
    Esp32BaseWeb::Method method;
    Esp32BaseWeb::Handler handler;
    bool appPage;
};

struct NativeTestStaticAsset {
    std::string path;
    std::string contentType;
    const uint8_t* data;
    size_t len;
    uint32_t cacheMaxAgeSec;
    bool authRequired;
};

struct NativeTestState {
    Esp32BaseWeb::Method method = Esp32BaseWeb::METHOD_UNKNOWN;
    std::string path;
    std::vector<NativeTestParam> params;
    std::string body;
    bool requestActive = false;
    bool authenticated = true;
    bool sameOrigin = true;
    bool authEnabled = true;
    std::string authUser = "admin";
    std::string authPass = "admin";
    std::string deviceName = "Esp32Base";
    std::string homePath;
    Esp32BaseWeb::HomeMode homeMode = Esp32BaseWeb::HOME_ESP32BASE;
    Esp32BaseWeb::SystemNavMode systemNavMode = Esp32BaseWeb::SYSTEM_NAV_SECTION;
    Esp32BaseWeb::FooterBarMode footerBarMode = Esp32BaseWeb::FOOTER_BAR_FULL;
    std::vector<NativeTestRoute> routes;
    std::vector<NativeTestStaticAsset> staticAssets;
    std::vector<Esp32BaseWeb::NativeTestHeader> navItems;
    Esp32BaseWeb::NativeTestResponse response{0, "", "", {}, false, false};
};

NativeTestState& nativeState() {
    static NativeTestState state;
    return state;
}

const char* nativeMethodName(Esp32BaseWeb::Method method) {
    switch (method) {
        case Esp32BaseWeb::METHOD_GET: return "GET";
        case Esp32BaseWeb::METHOD_POST: return "POST";
        case Esp32BaseWeb::METHOD_ANY: return "ANY";
        case Esp32BaseWeb::METHOD_UNKNOWN:
        default: return "UNKNOWN";
    }
}

bool headerNameEquals(const std::string& a, const char* b) {
    if (!b || a.size() != std::strlen(b)) {
        return false;
    }
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

bool copyTo(const std::string& value, char* out, size_t len) {
    if (!out || len == 0) {
        return false;
    }
    const size_t take = std::min(len - 1U, value.size());
    std::memcpy(out, value.data(), take);
    out[take] = '\0';
    return value.size() < len;
}

NativeTestParam* findParam(const char* name) {
    if (!name) {
        return nullptr;
    }
    NativeTestState& state = nativeState();
    for (NativeTestParam& param : state.params) {
        if (param.name == name) {
            return &param;
        }
    }
    return nullptr;
}

void clearResponse() {
    NativeTestState& state = nativeState();
    state.response.code = 0;
    state.response.contentType.clear();
    state.response.body.clear();
    state.response.headers.clear();
    state.response.started = false;
    state.response.ended = false;
}

void setResponse(int code, const char* contentType, const char* body, bool ended) {
    NativeTestState& state = nativeState();
    state.response.code = code;
    state.response.contentType = contentType ? contentType : "";
    state.response.body = body ? body : "";
    state.response.started = true;
    state.response.ended = ended;
}

void setResponseHeader(const char* name, const char* value) {
    if (!name || !name[0]) {
        return;
    }
    NativeTestState& state = nativeState();
    for (Esp32BaseWeb::NativeTestHeader& header : state.response.headers) {
        if (headerNameEquals(header.name, name)) {
            header.value = value ? value : "";
            return;
        }
    }
    state.response.headers.push_back({name, value ? value : ""});
}

bool validRoutePath(const char* path) {
    return path && path[0] == '/' && std::strlen(path) < 48;
}

bool validContentType(const char* contentType) {
    return contentType && contentType[0] && std::strlen(contentType) <= 63;
}

bool routeMatches(Esp32BaseWeb::Method routeMethod, Esp32BaseWeb::Method requestMethod) {
    return routeMethod == Esp32BaseWeb::METHOD_ANY || routeMethod == requestMethod;
}

bool addNativeRoute(const char* path, const char* title, Esp32BaseWeb::Method method,
                    Esp32BaseWeb::Handler handler, bool appPage) {
    if (!validRoutePath(path) || !handler ||
        (method != Esp32BaseWeb::METHOD_GET && method != Esp32BaseWeb::METHOD_POST &&
         method != Esp32BaseWeb::METHOD_ANY)) {
        return false;
    }
    nativeState().routes.push_back({path, title ? title : "", method, handler, appPage});
    return true;
}

NativeTestStaticAsset* findNativeStaticAsset(const char* path) {
    if (!path) {
        return nullptr;
    }
    for (NativeTestStaticAsset& asset : nativeState().staticAssets) {
        if (asset.path == path) {
            return &asset;
        }
    }
    return nullptr;
}

void sendEscapedHtmlNative(const char* text);
void sendEscapedJsonNative(const char* text);

void sendSimpleWrappedText(const char* before, const char* text, const char* after) {
    Esp32BaseWeb::sendChunk(before);
    sendEscapedHtmlNative(text ? text : "");
    Esp32BaseWeb::sendChunk(after);
}

void sendEscapedHtmlNative(const char* text) {
    if (!text) {
        return;
    }
    for (const char* p = text; *p; ++p) {
        switch (*p) {
            case '&': Esp32BaseWeb::sendChunk("&amp;"); break;
            case '<': Esp32BaseWeb::sendChunk("&lt;"); break;
            case '>': Esp32BaseWeb::sendChunk("&gt;"); break;
            case '"': Esp32BaseWeb::sendChunk("&quot;"); break;
            case '\'': Esp32BaseWeb::sendChunk("&#39;"); break;
            default: {
                char one[2] = {*p, '\0'};
                Esp32BaseWeb::sendChunk(one);
                break;
            }
        }
    }
}

void sendEscapedJsonNative(const char* text) {
    if (!text) {
        return;
    }
    for (const char* p = text; *p; ++p) {
        switch (*p) {
            case '"': Esp32BaseWeb::sendChunk("\\\""); break;
            case '\\': Esp32BaseWeb::sendChunk("\\\\"); break;
            case '\n': Esp32BaseWeb::sendChunk("\\n"); break;
            case '\r': Esp32BaseWeb::sendChunk("\\r"); break;
            case '\t': Esp32BaseWeb::sendChunk("\\t"); break;
            default:
                if (static_cast<unsigned char>(*p) < 0x20) {
                    char buf[7];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(*p));
                    Esp32BaseWeb::sendChunk(buf);
                } else {
                    char one[2] = {*p, '\0'};
                    Esp32BaseWeb::sendChunk(one);
                }
                break;
        }
    }
}

} // namespace

bool Esp32BaseWeb::begin() {
    return true;
}

void Esp32BaseWeb::handle() {
}

bool Esp32BaseWeb::isReady() {
    return true;
}

bool Esp32BaseWeb::startLocked() {
    return false;
}

void Esp32BaseWeb::setDefaultAuth(const char* user, const char* pass) {
    if (user && user[0]) {
        nativeState().authUser = user;
    }
    if (pass && pass[0]) {
        nativeState().authPass = pass;
    }
}

const char* Esp32BaseWeb::authUser() {
    return nativeState().authUser.c_str();
}

const char* Esp32BaseWeb::authPassword() {
    return nativeState().authPass.c_str();
}

bool Esp32BaseWeb::isAuthEnabled() {
    return nativeState().authEnabled;
}

void Esp32BaseWeb::setAuthEnabled(bool enabled) {
    nativeState().authEnabled = enabled;
}

bool Esp32BaseWeb::checkAuth() {
    NativeTestState& state = nativeState();
    if (!state.authEnabled || state.authenticated) {
        return true;
    }
    setResponseHeader("WWW-Authenticate", "Basic realm=\"Esp32Base\"");
    sendText(401, "Authentication Required");
    return false;
}

bool Esp32BaseWeb::checkPostAllowed(const char*) {
    NativeTestState& state = nativeState();
    if (state.method != METHOD_POST) {
        sendText(405, "Method Not Allowed");
        return false;
    }
    if (!checkAuth()) {
        return false;
    }
    if (!state.sameOrigin) {
        sendText(403, "Forbidden");
        return false;
    }
    return true;
}

bool Esp32BaseWeb::verifyAuth() {
    return !nativeState().authEnabled || nativeState().authenticated;
}

bool Esp32BaseWeb::verifyAuth(const char* user, const char* pass) {
    NativeTestState& state = nativeState();
    return user && pass && state.authUser == user && state.authPass == pass;
}

bool Esp32BaseWeb::saveAuth(const char* user, const char* pass) {
    if (!user || !user[0] || !pass || !pass[0]) {
        return false;
    }
    nativeState().authUser = user;
    nativeState().authPass = pass;
    return true;
}

bool Esp32BaseWeb::resetAuth() {
    nativeState().authUser = "admin";
    nativeState().authPass = "admin";
    nativeState().authenticated = true;
    return true;
}

bool Esp32BaseWeb::addRoute(const char* path, Method method, Handler handler) {
    return addNativeRoute(path, nullptr, method, handler, false);
}

bool Esp32BaseWeb::addPage(const char* path, const char* title, Handler handler) {
    return addNativeRoute(path, title, METHOD_GET, handler, true);
}

bool Esp32BaseWeb::addApi(const char* path, Handler handler) {
    return addRoute(path, METHOD_ANY, handler);
}

bool Esp32BaseWeb::addStaticAsset(const char* path, const char* contentType, const uint8_t* data, size_t len,
                                  uint32_t cacheMaxAgeSec, bool authRequired) {
    if (!validRoutePath(path) || !validContentType(contentType) || !data || len == 0 || findNativeStaticAsset(path)) {
        return false;
    }
    nativeState().staticAssets.push_back({path, contentType, data, len, cacheMaxAgeSec, authRequired});
    return true;
}

bool Esp32BaseWeb::addNavItem(const char* path, const char* title) {
    if (!validRoutePath(path) || !title || !title[0]) {
        return false;
    }
    nativeState().navItems.push_back({path, title});
    return true;
}

bool Esp32BaseWeb::setDeviceName(const char* name) {
    if (!name || !name[0]) {
        return false;
    }
    nativeState().deviceName = name;
    return true;
}

bool Esp32BaseWeb::setHomePath(const char* path) {
    if (!validRoutePath(path)) {
        return false;
    }
    nativeState().homePath = path;
    return true;
}

void Esp32BaseWeb::setHomeMode(HomeMode mode) {
    nativeState().homeMode = mode;
}

void Esp32BaseWeb::setSystemNavMode(SystemNavMode mode) {
    nativeState().systemNavMode = mode;
}

bool Esp32BaseWeb::setFooterBarMode(FooterBarMode mode) {
    nativeState().footerBarMode = mode;
    return true;
}

Esp32BaseWeb::FooterBarMode Esp32BaseWeb::footerBarMode() {
    return nativeState().footerBarMode;
}

const char* Esp32BaseWeb::footerBarModeName() {
    switch (nativeState().footerBarMode) {
        case FOOTER_BAR_OFF: return "Off";
        case FOOTER_BAR_STATUS_ONLY: return "Status only";
        case FOOTER_BAR_FULL:
        default: return "Links + status";
    }
}

bool Esp32BaseWeb::setBuiltinLabel(BuiltinPage, const char*) {
    return true;
}

void Esp32BaseWeb::setHeadExtraCallback(Handler) {
}

Esp32BaseWeb::Method Esp32BaseWeb::currentMethod() {
    return nativeState().requestActive ? nativeState().method : METHOD_UNKNOWN;
}

bool Esp32BaseWeb::isMethod(Method method) {
    const Method current = currentMethod();
    return method == METHOD_ANY ? current != METHOD_UNKNOWN : current == method;
}

const char* Esp32BaseWeb::currentMethodName() {
    return nativeMethodName(currentMethod());
}

bool Esp32BaseWeb::hasParam(const char* name) {
    return findParam(name) != nullptr;
}

bool Esp32BaseWeb::getParam(const char* name, char* out, size_t len) {
    NativeTestParam* param = findParam(name);
    if (!param || !out || len == 0) {
        return false;
    }
    return copyTo(param->value, out, len);
}

bool Esp32BaseWeb::getRequestBody(char* out, size_t len) {
    if (!out || len == 0) {
        return false;
    }
    return copyTo(nativeState().body, out, len);
}

void Esp32BaseWeb::sendHeader(const char* title) {
    if (!beginResponse(200, "text/html; charset=utf-8", nullptr)) {
        return;
    }
    sendChunk("<!doctype html><html><head><title>");
    writeHtmlEscaped(title ? title : nativeState().deviceName.c_str());
    sendChunk("</title></head><body><main class='page'>");
}

void Esp32BaseWeb::sendFooter() {
    sendChunk("</main></body></html>");
    endResponse();
}

void Esp32BaseWeb::sendPageTitle(const char* title, const char* subtitle) {
    sendSimpleWrappedText("<header class='pagehead'><h1>", title, "</h1>");
    if (subtitle && subtitle[0]) {
        sendSimpleWrappedText("<p>", subtitle, "</p>");
    }
    sendChunk("</header>");
}

void Esp32BaseWeb::beginPanel(const char* title) {
    sendChunk("<section class='panel'>");
    if (title && title[0]) {
        sendSimpleWrappedText("<h2>", title, "</h2>");
    }
}

void Esp32BaseWeb::endPanel() {
    sendChunk("</section>");
}

void Esp32BaseWeb::sendNotice(UiTone, const char* title, const char* message) {
    sendSimpleWrappedText("<div class='notice'><b>", title, "</b>");
    if (message && message[0]) {
        sendSimpleWrappedText("<br>", message, "");
    }
    sendChunk("</div>");
}

void Esp32BaseWeb::sendResultNotice(const ResultNotice* notices, uint8_t count) {
    if (!notices) {
        return;
    }
    char value[32];
    for (uint8_t i = 0; i < count; ++i) {
        const ResultNotice& notice = notices[i];
        if (!notice.param || !hasParam(notice.param)) {
            continue;
        }
        if (notice.value && notice.value[0]) {
            if (!getParam(notice.param, value, sizeof(value)) || std::strcmp(value, notice.value) != 0) {
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
    sendSimpleWrappedText("<div class='metric'><b>", value, "</b><span>");
    writeHtmlEscaped(label ? label : "");
    if (help && help[0]) {
        sendChunk(" ");
        writeHtmlEscaped(help);
    }
    sendChunk("</span></div>");
}

void Esp32BaseWeb::endMetricGrid() {
    sendChunk("</div>");
}

void Esp32BaseWeb::sendInfoRowCompact(const char* title, const char* help, const char* value) {
    sendSimpleWrappedText("<div class='urow'><b>", title, "</b>");
    if (help && help[0]) {
        sendSimpleWrappedText("<small>", help, "</small>");
    }
    if (value && value[0]) {
        sendSimpleWrappedText("<span>", value, "</span>");
    }
    sendChunk("</div>");
}

void Esp32BaseWeb::sendInfoRowCompactLink(const char* title, const char* help, const char* value,
                                          const char* href, const char* label, UiTone) {
    sendInfoRowCompact(title, help, value);
    sendChunk("<a href='");
    writeHtmlEscaped(href ? href : "#");
    sendChunk("'>");
    writeHtmlEscaped(label ? label : "");
    sendChunk("</a>");
}

void Esp32BaseWeb::sendInfoRowCompactForm(const char* title, const char* help, const char* value,
                                          const char* action, const char* label,
                                          const char*, const char*, UiTone) {
    sendInfoRowCompact(title, help, value);
    sendChunk("<form method='post' action='");
    writeHtmlEscaped(action ? action : "");
    sendChunk("'><input type='submit' value='");
    writeHtmlEscaped(label ? label : "");
    sendChunk("'></form>");
}

void Esp32BaseWeb::sendInfoRowInlineEdit(const char*, const char* title, const char* help, const char* value,
                                         const char* action, const char* inputName, const char* inputValue,
                                         const char* label, UiTone tone) {
    sendInfoRowCompactForm(title, help, value, action, label, inputName, inputValue, tone);
}

void Esp32BaseWeb::sendInfoRowDialogForm(const char*, const char*, const char* title,
                                         const char* help, const char* value, const char* action,
                                         const char*, const char* label, UiTone tone) {
    sendInfoRowCompactForm(title, help, value, action, label, nullptr, nullptr, tone);
}

void Esp32BaseWeb::sendPagination(const Pagination&) {
    sendChunk("<div class='pagination'></div>");
}

bool Esp32BaseWeb::isAjaxRequest() {
    NativeTestParam* param = findParam("_ajax");
    return param && param->value == "1";
}

void Esp32BaseWeb::sendAjaxReplace(const char* targetId, const char* html, const char* noticeTitle,
                                   UiTone, bool close) {
    if (!beginResponse(200, "application/json", nullptr)) {
        return;
    }
    sendChunk("{\"ok\":true,\"target\":\"");
    writeJsonEscaped(targetId ? targetId : "");
    sendChunk("\",\"html\":\"");
    writeJsonEscaped(html ? html : "");
    sendChunk("\",\"close\":");
    sendChunk(close ? "true" : "false");
    if (noticeTitle && noticeTitle[0]) {
        sendChunk(",\"notice\":{\"title\":\"");
        writeJsonEscaped(noticeTitle);
        sendChunk("\"}");
    }
    sendChunk("}");
    endResponse();
}

void Esp32BaseWeb::sendAjaxError(int code, const char* error) {
    if (!beginResponse(code, "application/json", nullptr)) {
        return;
    }
    sendChunk("{\"ok\":false,\"error\":\"");
    writeJsonEscaped(error ? error : "Action failed");
    sendChunk("\"}");
    endResponse();
}

bool Esp32BaseWeb::sendResponseHeader(const char* name, const char* value) {
    if (!nativeState().requestActive || nativeState().response.started || !name || !name[0] || !value) {
        return false;
    }
    setResponseHeader(name, value);
    return true;
}

bool Esp32BaseWeb::beginResponse(int code, const char* contentType, const char* filename) {
    if (!nativeState().requestActive || !contentType || !contentType[0]) {
        return false;
    }
    setResponse(code, contentType, "", false);
    if (filename && filename[0]) {
        std::string disposition = "attachment; filename=\"";
        disposition += filename;
        disposition += "\"";
        setResponseHeader("Content-Disposition", disposition.c_str());
    }
    return true;
}

bool Esp32BaseWeb::beginText(int code) {
    return beginResponse(code, "text/plain; charset=utf-8", nullptr);
}

bool Esp32BaseWeb::beginCsv(int code, const char* filename) {
    return beginResponse(code, "text/csv; charset=utf-8", filename);
}

void Esp32BaseWeb::endResponse() {
    nativeState().response.ended = true;
}

void Esp32BaseWeb::sendChunk(const char* text) {
    if (!text || !nativeState().response.started) {
        return;
    }
    nativeState().response.body += text;
}

void Esp32BaseWeb::sendBytes(const uint8_t* data, size_t len) {
    if ((!data && len > 0) || !nativeState().response.started) {
        return;
    }
    nativeState().response.body.append(reinterpret_cast<const char*>(data), len);
}

void Esp32BaseWeb::writeHtmlEscaped(const char* text) {
    sendEscapedHtmlNative(text);
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
    setResponse(code, "text/plain", text ? text : "", true);
}

void Esp32BaseWeb::sendHtml(int code, const char* html) {
    setResponse(code, "text/html", html ? html : "", true);
}

void Esp32BaseWeb::sendJson(int code, const char* json) {
    setResponse(code, "application/json", json ? json : "{}", true);
}

void Esp32BaseWeb::redirectSeeOther(const char* url) {
    setResponseHeader("Location", url ? url : "/esp32base");
    setResponseHeader("Cache-Control", "no-store");
    setResponse(303, "text/plain", "", true);
}

void Esp32BaseWeb::beginJson(int code) {
    if (beginResponse(code, "application/json", nullptr)) {
        sendChunk("{");
    }
}

void Esp32BaseWeb::writeJsonEscaped(const char* text) {
    sendEscapedJsonNative(text);
}

void Esp32BaseWeb::endJson() {
    sendChunk("}");
    endResponse();
}

void Esp32BaseWeb::nativeTestReset() {
    clearAfterFormatFsCallback();
    NativeTestState& state = nativeState();
    state.method = METHOD_UNKNOWN;
    state.path.clear();
    state.params.clear();
    state.body.clear();
    state.requestActive = false;
    state.authenticated = true;
    state.sameOrigin = true;
    state.authEnabled = true;
    state.authUser = "admin";
    state.authPass = "admin";
    state.deviceName = "Esp32Base";
    state.homePath.clear();
    state.homeMode = HOME_ESP32BASE;
    state.systemNavMode = SYSTEM_NAV_SECTION;
    state.footerBarMode = FOOTER_BAR_FULL;
    state.routes.clear();
    state.staticAssets.clear();
    state.navItems.clear();
    clearResponse();
}

void Esp32BaseWeb::nativeTestBeginRequest(Method method, const char* path) {
    NativeTestState& state = nativeState();
    state.method = method;
    state.path = path && path[0] ? path : "/";
    state.params.clear();
    state.body.clear();
    state.requestActive = true;
    clearResponse();
}

void Esp32BaseWeb::nativeTestSetParam(const char* name, const char* value) {
    if (!name || !name[0]) {
        return;
    }
    NativeTestParam* existing = findParam(name);
    if (existing) {
        existing->value = value ? value : "";
        return;
    }
    nativeState().params.push_back({name, value ? value : ""});
}

void Esp32BaseWeb::nativeTestSetBody(const char* body) {
    nativeState().body = body ? body : "";
}

void Esp32BaseWeb::nativeTestSetAuthenticated(bool authenticated) {
    nativeState().authenticated = authenticated;
}

void Esp32BaseWeb::nativeTestSetSameOrigin(bool sameOrigin) {
    nativeState().sameOrigin = sameOrigin;
}

bool Esp32BaseWeb::nativeTestRun(Handler handler) {
    if (!handler) {
        return false;
    }
    NativeTestState& state = nativeState();
    if (!state.requestActive) {
        nativeTestBeginRequest(METHOD_GET, "/");
    }
    handler();
    return true;
}

bool Esp32BaseWeb::nativeTestDispatch(const char* path, Method method) {
    NativeTestState& state = nativeState();
    for (const NativeTestRoute& route : state.routes) {
        if (route.path == (path ? path : "") && routeMatches(route.method, method)) {
            state.method = method;
            state.path = path && path[0] ? path : "/";
            state.requestActive = true;
            clearResponse();
            route.handler();
            return true;
        }
    }
    NativeTestStaticAsset* asset = findNativeStaticAsset(path);
    if (asset && method == METHOD_GET) {
        state.method = method;
        state.path = path && path[0] ? path : "/";
        state.requestActive = true;
        clearResponse();
        if (asset->authRequired && !checkAuth()) {
            return true;
        }
        char cacheControl[40];
        if (asset->cacheMaxAgeSec > 0) {
            std::snprintf(cacheControl, sizeof(cacheControl), "%s, max-age=%lu",
                          asset->authRequired ? "private" : "public",
                          static_cast<unsigned long>(asset->cacheMaxAgeSec));
        } else {
            std::snprintf(cacheControl, sizeof(cacheControl), "no-store");
        }
        setResponseHeader("Cache-Control", cacheControl);
        setResponseHeader("X-Content-Type-Options", "nosniff");
        setResponse(200, asset->contentType.c_str(), "", true);
        state.response.body.assign(reinterpret_cast<const char*>(asset->data), asset->len);
        return true;
    }
    state.method = method;
    state.path = path && path[0] ? path : "/";
    state.requestActive = true;
    clearResponse();
    sendText(404, "Not found");
    return false;
}

const Esp32BaseWeb::NativeTestResponse& Esp32BaseWeb::nativeTestResponse() {
    return nativeState().response;
}

const char* Esp32BaseWeb::nativeTestResponseHeader(const char* name) {
    static const char empty[] = "";
    if (!name) {
        return empty;
    }
    for (const NativeTestHeader& header : nativeState().response.headers) {
        if (headerNameEquals(header.name, name)) {
            return header.value.c_str();
        }
    }
    return empty;
}

void Esp32BaseWeb::nativeTestNotifyToolsFormatFsSuccess(bool mountSuccess, bool fileLogReloadSuccess) {
    dispatchToolsFormatFsSuccess(mountSuccess, fileLogReloadSuccess, 0, 0, false);
}

#elif ESP32BASE_ENABLE_WEB

#include "internal/WebInternal.h"

using namespace esp32base_web;

namespace esp32base_web {
void notifyToolsFormatFsSuccess(bool mountSuccess,
                                bool fileLogReloadSuccess,
                                uint8_t businessRecordStoreCount,
                                uint8_t businessRecordStoreReloadedCount) {
    dispatchToolsFormatFsSuccess(mountSuccess,
                                 fileLogReloadSuccess,
                                 businessRecordStoreCount,
                                 businessRecordStoreReloadedCount,
                                 true);
}
#if ESP32BASE_ENABLE_RECORD_STORE
uint8_t businessRecordStoreCount() {
    return g_businessRecordStoreCount;
}

Esp32BaseRecordStore* businessRecordStoreAt(uint8_t index) {
    return index < g_businessRecordStoreCount ? g_businessRecordStores[index] : nullptr;
}
#endif
}

bool Esp32BaseWeb::begin() {
    if (g_webReady) {
        return true;
    }
    if (g_startLocked) {
        return false;
    }
    const uint8_t appRoutes = routeCount(false);
    const uint8_t appPages = routeCount(true);
    const uint8_t staticAssets = staticAssetCount();
    ESP32BASE_LOG_D("web", "server_registering app_routes=%u app_pages=%u static_assets=%u",
                    static_cast<unsigned>(appRoutes),
                    static_cast<unsigned>(appPages),
                    static_cast<unsigned>(staticAssets));
    if (!loadStoredAuth()) {
        if (!applyDefaultAuth()) {
            g_startLocked = true;
            ESP32BASE_LOG_E("web", "auth_default_missing action=server_not_started");
            return false;
        }
#if ESP32BASE_WEB_ALLOW_INSECURE_DEFAULT_AUTH
        if (!g_defaultAuthSet) {
            ESP32BASE_LOG_W("web", "auth_loaded user=%s password_set=%s source=insecure_builtin",
                            g_authUser,
                            g_authPass[0] ? "yes" : "no");
        } else
#endif
        {
            ESP32BASE_LOG_I("web", "auth_loaded user=%s password_set=%s source=default",
                            g_authUser,
                            g_authPass[0] ? "yes" : "no");
        }
    } else {
        g_startLocked = false;
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
#if ESP32BASE_ENABLE_APP_EVENTS
    g_server.on("/esp32base/app-events", HTTP_GET, handleAppEventsPage);
    g_server.on("/esp32base/api/app-events", HTTP_GET, handleAppEventsApi);
    g_server.on("/esp32base/app-events.csv", HTTP_GET, handleAppEventsCsv);
#endif
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
#if ESP32BASE_ENABLE_RECORD_STORE
    g_server.on("/esp32base/tools/business-records-clear", HTTP_POST, handleToolsBusinessRecordsClearPost);
#endif
#if ESP32BASE_ENABLE_APP_EVENTS
    g_server.on("/esp32base/tools/app-events-clear", HTTP_POST, handleToolsAppEventsClearPost);
#endif
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
    for (uint8_t i = 0; i < ESP32BASE_WEB_MAX_STATIC_ASSETS; ++i) {
        if (g_staticAssets[i].path[0] && !g_staticAssets[i].registered) {
            registerStaticAsset(g_staticAssets[i]);
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
                ESP32BASE_LOG_I("web", "slow_request method=%s uri=%s elapsed=%lu ms",
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

bool Esp32BaseWeb::startLocked() {
    return g_startLocked;
}

void Esp32BaseWeb::setDefaultAuth(const char* user, const char* pass) {
    if (!validAuthUser(user) || !validAuthPass(pass)) {
        ESP32BASE_LOG_W("web", "set_default_auth_failed reason=invalid");
        return;
    }
    strlcpy(g_defaultAuthUser, user, sizeof(g_defaultAuthUser));
    strlcpy(g_defaultAuthPass, pass, sizeof(g_defaultAuthPass));
    g_defaultAuthSet = true;
    g_startLocked = false;
    if (!g_authLoadedFromStorage) {
        applyDefaultAuth();
    }
    ESP32BASE_LOG_I("web", "default_auth_set user=%s password_set=%s applied=%s",
                    g_defaultAuthUser,
                    g_defaultAuthPass[0] ? "yes" : "no",
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

bool Esp32BaseWeb::checkPostAllowed(const char* context) {
    return ensurePostAllowed(context);
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
    g_startLocked = false;
    ESP32BASE_LOG_I("web", "auth_saved user=%s password_set=%s",
                    g_authUser,
                    g_authPass[0] ? "yes" : "no");
    return true;
}

bool Esp32BaseWeb::resetAuth() {
    const bool ok = Esp32BaseConfig::clearNamespace("eb_web");
    const bool hasDefault = applyDefaultAuth();
    if (hasDefault) {
        g_startLocked = false;
        ESP32BASE_LOG_I("web", "auth_loaded user=%s password_set=%s source=default",
                        g_authUser,
                        g_authPass[0] ? "yes" : "no");
    } else {
        g_startLocked = true;
        ESP32BASE_LOG_W("web", "auth_default_missing action=auth_reset_locked");
    }
    ESP32BASE_LOG_I("web", "auth_reset");
    return ok && hasDefault;
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

bool Esp32BaseWeb::addStaticAsset(const char* path, const char* contentType, const uint8_t* data, size_t len,
                                  uint32_t cacheMaxAgeSec, bool authRequired) {
    if (!path || path[0] != '/' || strlen(path) >= sizeof(g_staticAssets[0].path) ||
        !validHeaderValue(contentType, 63) || !data || len == 0 ||
        isBuiltinWebPath(path) || findStaticAsset(path) || findRoute(path, METHOD_GET) || findRoute(path, METHOD_ANY)) {
        return false;
    }
    for (uint8_t i = 0; i < ESP32BASE_WEB_MAX_STATIC_ASSETS; ++i) {
        if (!g_staticAssets[i].path[0]) {
            strlcpy(g_staticAssets[i].path, path, sizeof(g_staticAssets[i].path));
            g_staticAssets[i].contentType = contentType;
            g_staticAssets[i].data = data;
            g_staticAssets[i].len = len;
            g_staticAssets[i].cacheMaxAgeSec = cacheMaxAgeSec;
            g_staticAssets[i].authRequired = authRequired;
            g_staticAssets[i].registered = false;
            if (g_webReady) {
                registerStaticAsset(g_staticAssets[i]);
            }
            ESP32BASE_LOG_D("web", "static_asset_registered path=%s bytes=%lu cache=%lu auth=%s",
                            g_staticAssets[i].path,
                            static_cast<unsigned long>(len),
                            static_cast<unsigned long>(cacheMaxAgeSec),
                            authRequired ? "yes" : "no");
            return true;
        }
    }
    return false;
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
    const String value = g_server.arg(name);
    return strlcpy(out, value.c_str(), len) < len;
}

bool Esp32BaseWeb::getRequestBody(char* out, size_t len) {
    if (!out || len == 0) {
        return false;
    }
    const String value = g_server.arg("plain");
    return strlcpy(out, value.c_str(), len) < len;
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

void Esp32BaseWeb::sendInfoRowInlineEdit(const char* id, const char* title, const char* help, const char* value,
                                         const char* action, const char* inputName, const char* inputValue,
                                         const char* label, UiTone tone) {
    const char* rowId = id && id[0] ? id : "eb-inline-row";
    sendChunk("<div id='");
    sendEscapedHtmlChunk(rowId);
    sendChunk("'>");
    sendInfoRowCompactStart(title, help, value, true);
    sendChunk("<button type='button' class='btnlink");
    sendChunk(uiToneClass(tone));
    sendChunk("' data-eb-inline-toggle='");
    sendEscapedHtmlChunk(rowId);
    sendChunk("-edit'>");
    sendEscapedHtmlChunk(label ? label : "Edit");
    sendChunk("</button>");
    sendInfoRowCompactEnd();
    sendChunk("<div id='");
    sendEscapedHtmlChunk(rowId);
    sendChunk("-edit' class='eb-inline-edit'><form method='post' data-eb-ajax action='");
    sendEscapedHtmlChunk(action ? action : "");
    sendChunk("'><label>");
    sendEscapedHtmlChunk(title ? title : "");
    sendChunk("</label><input name='");
    sendEscapedHtmlChunk(inputName ? inputName : "value");
    sendChunk("' value='");
    sendEscapedHtmlChunk(inputValue ? inputValue : "");
    sendChunk("' autocomplete='off'><div data-eb-error class='eb-inline-error'></div><div class='actions'><button type='button' class='secondary' data-eb-inline-close='");
    sendEscapedHtmlChunk(rowId);
    sendChunk("-edit'>Cancel</button><input type='submit' value='Save'></div></form></div></div>");
}

void Esp32BaseWeb::sendInfoRowDialogForm(const char* dialogId, const char* targetId, const char* title,
                                         const char* help, const char* value, const char* action,
                                         const char* fieldsHtml, const char* label, UiTone tone) {
    const char* id = dialogId && dialogId[0] ? dialogId : "eb-dialog";
    sendChunk("<div id='");
    sendEscapedHtmlChunk(targetId && targetId[0] ? targetId : id);
    sendChunk("'>");
    sendInfoRowCompactStart(title, help, value, true);
    sendChunk("<button type='button' class='btnlink");
    sendChunk(uiToneClass(tone));
    sendChunk("' data-eb-dialog-open='");
    sendEscapedHtmlChunk(id);
    sendChunk("'>");
    sendEscapedHtmlChunk(label ? label : "Edit");
    sendChunk("</button>");
    sendInfoRowCompactEnd();
    sendChunk("</div><div id='");
    sendEscapedHtmlChunk(id);
    sendChunk("' class='eb-dialog-backdrop'><div class='eb-dialog'><button type='button' class='eb-dialog-close' data-eb-dialog-close>Close</button><h2>");
    sendEscapedHtmlChunk(title ? title : "");
    sendChunk("</h2><form method='post' data-eb-ajax action='");
    sendEscapedHtmlChunk(action ? action : "");
    sendChunk("'>");
    if (fieldsHtml && fieldsHtml[0]) {
        sendChunk(fieldsHtml);
    }
    sendChunk("<div data-eb-error class='eb-dialog-error'></div><div class='actions'><button type='button' class='secondary' data-eb-dialog-close>Cancel</button><input type='submit' value='Save'></div></form></div></div>");
}

bool Esp32BaseWeb::isAjaxRequest() {
    return g_server.header("X-Esp32Base-Ajax") == "1";
}

static const char* ajaxToneName(Esp32BaseWeb::UiTone tone) {
    switch (tone) {
        case Esp32BaseWeb::UI_OK: return "ok";
        case Esp32BaseWeb::UI_WARN: return "warn";
        case Esp32BaseWeb::UI_DANGER: return "danger";
        case Esp32BaseWeb::UI_INFO: return "info";
        case Esp32BaseWeb::UI_NEUTRAL:
        default: return "neutral";
    }
}

void Esp32BaseWeb::sendAjaxReplace(const char* targetId, const char* html, const char* noticeTitle,
                                   UiTone tone, bool close) {
    if (!beginResponse(200, "application/json", nullptr)) {
        return;
    }
    sendChunk("{\"ok\":true,\"target\":\"");
    sendEscapedJsonChunk(targetId ? targetId : "");
    sendChunk("\",\"html\":\"");
    sendEscapedJsonChunk(html ? html : "");
    sendChunk("\"");
    if (noticeTitle && noticeTitle[0]) {
        sendChunk(",\"notice\":{\"tone\":\"");
        sendChunk(ajaxToneName(tone));
        sendChunk("\",\"title\":\"");
        sendEscapedJsonChunk(noticeTitle);
        sendChunk("\"}");
    }
    sendChunk(",\"close\":");
    sendChunk(close ? "true" : "false");
    sendChunk("}");
    endResponse();
}

void Esp32BaseWeb::sendAjaxError(int code, const char* error) {
    if (!beginResponse(code, "application/json", nullptr)) {
        return;
    }
    sendChunk("{\"ok\":false,\"error\":\"");
    sendEscapedJsonChunk(error ? error : "Action failed");
    sendChunk("\"}");
    endResponse();
}

void Esp32BaseWeb::sendPagination(const Pagination& pagination) {
    const uint32_t perPage = pagination.perPage == 0 ? 10 : pagination.perPage;
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
    const uint16_t options[] = {10, 15, 20, 30, 50};
    for (uint8_t i = 0; i < 5; ++i) {
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
        const char first = text[0];
        if (first == '=' || first == '+' || first == '-' || first == '@' ||
            first == '\t' || first == '\r' || first == '\n') {
            sendChunk("'");
        }
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
