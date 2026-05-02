#include "Esp32BaseWeb.h"

#if !defined(ESP32)
#error "Esp32Base supports ESP32 Arduino Core targets only."
#endif

#include "Esp32Base.h"
#include "Esp32BaseLog.h"
#include "Esp32BaseNetwork.h"
#include "Esp32BaseRuntime.h"

#include <Update.h>
#include <WebServer.h>
#include <string.h>

namespace {

WebServer g_server(80);
bool g_otaOk = false;

const char kIndexHtml[] PROGMEM =
    "<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Esp32Base</title></head><body><h1>Esp32Base</h1><ul>"
    "<li><a href='/esp32base/api/status'>Status API</a></li>"
    "<li><a href='/esp32base/wifi'>WiFi</a></li>"
    "<li><a href='/esp32base/ota'>OTA</a></li>"
    "</ul></body></html>";

const char kWifiHtml[] PROGMEM =
    "<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>WiFi</title></head><body><h1>WiFi</h1>"
    "<form method='post' action='/esp32base/wifi'>"
    "<p><input name='ssid' placeholder='SSID'></p>"
    "<p><input name='pass' placeholder='Password' type='password'></p>"
    "<p><button type='submit'>Save</button></p>"
    "</form><p><a href='/esp32base'>Back</a></p></body></html>";

const char kOtaHtml[] PROGMEM =
    "<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>OTA</title></head><body><h1>OTA</h1>"
    "<form method='post' action='/esp32base/ota' enctype='multipart/form-data'>"
    "<p><input type='file' name='firmware'></p>"
    "<p><button type='submit'>Upload</button></p>"
    "</form><p><a href='/esp32base'>Back</a></p></body></html>";

}  // namespace

bool Esp32BaseWeb::_ready = false;
bool Esp32BaseWeb::_authEnabled = true;
char Esp32BaseWeb::_authUser[32] = ESP32BASE_WEB_AUTH_USER;
char Esp32BaseWeb::_authPass[32] = ESP32BASE_WEB_AUTH_PASS;
Esp32BaseWeb::Route Esp32BaseWeb::_routes[ESP32BASE_WEB_MAX_APP_ROUTES] = {};

bool Esp32BaseWeb::begin() {
    if (_ready) {
        return true;
    }

    registerBuiltInRoutes();
    g_server.onNotFound(handleNotFound);
    g_server.begin();
    _ready = true;
    logConfig();
    return true;
}

void Esp32BaseWeb::handle() {
    if (!_ready) {
        return;
    }

    g_server.handleClient();
}

bool Esp32BaseWeb::isReady() {
    return _ready;
}

void Esp32BaseWeb::setAuth(const char* user, const char* pass) {
    copyText(user, _authUser, sizeof(_authUser));
    copyText(pass, _authPass, sizeof(_authPass));
}

void Esp32BaseWeb::setAuthEnabled(bool enabled) {
    _authEnabled = enabled;
}

bool Esp32BaseWeb::addPage(const char* path, Esp32BaseWebHandler handler) {
    return addRouteInternal(path, METHOD_GET, handler);
}

bool Esp32BaseWeb::addApi(const char* path, Esp32BaseWebHandler handler) {
    return addRouteInternal(path, METHOD_ANY, handler);
}

bool Esp32BaseWeb::addRoute(const char* path, Method method, Esp32BaseWebHandler handler) {
    return addRouteInternal(path, method, handler);
}

bool Esp32BaseWeb::hasParam(const char* name) {
    return name != nullptr && g_server.hasArg(name);
}

bool Esp32BaseWeb::getParam(const char* name, char* out, size_t len) {
    if (out == nullptr || len == 0U) {
        return false;
    }

    out[0] = '\0';
    if (name == nullptr || !g_server.hasArg(name)) {
        return false;
    }

    copyText(g_server.arg(name).c_str(), out, len);
    return true;
}

bool Esp32BaseWeb::getRequestBody(char* out, size_t len) {
    return getParam("plain", out, len);
}

void Esp32BaseWeb::sendText(int code, const char* text) {
    g_server.send(code, "text/plain; charset=utf-8", text == nullptr ? "" : text);
}

void Esp32BaseWeb::sendJson(int code, const char* json) {
    g_server.send(code, "application/json", json == nullptr ? "{}" : json);
}

void Esp32BaseWeb::sendHtml(int code, const char* html) {
    g_server.send(code, "text/html; charset=utf-8", html == nullptr ? "" : html);
}

void Esp32BaseWeb::sendContent(const char* text) {
    g_server.sendContent(text == nullptr ? "" : text);
}

void Esp32BaseWeb::sendContentP(const char* progmemText) {
    g_server.sendContent_P(progmemText == nullptr ? PSTR("") : progmemText);
}

void Esp32BaseWeb::send404() {
    sendJson(404, "{\"ok\":false,\"error\":\"not_found\"}");
}

uint8_t Esp32BaseWeb::routeCount() {
    uint8_t count = 0;
    for (uint8_t i = 0; i < routeCapacity(); ++i) {
        if (_routes[i].used) {
            ++count;
        }
    }
    return count;
}

uint8_t Esp32BaseWeb::routeCapacity() {
    return static_cast<uint8_t>(ESP32BASE_WEB_MAX_APP_ROUTES);
}

void Esp32BaseWeb::logConfig() {
    ESP32BASE_LOG_I("BaseWeb", "ready=%u auth=%u routes=%u/%u",
                    _ready ? 1U : 0U,
                    _authEnabled ? 1U : 0U,
                    static_cast<unsigned>(routeCount()),
                    static_cast<unsigned>(routeCapacity()));
}

bool Esp32BaseWeb::addRouteInternal(const char* path, Method method, Esp32BaseWebHandler handler) {
    if (path == nullptr || path[0] != '/' || strlen(path) >= ESP32BASE_WEB_PATH_LEN || handler == nullptr) {
        return false;
    }

    for (uint8_t i = 0; i < routeCapacity(); ++i) {
        if (_routes[i].used && strcmp(_routes[i].path, path) == 0 && _routes[i].method == method) {
            _routes[i].handler = handler;
            return true;
        }
    }

    for (uint8_t i = 0; i < routeCapacity(); ++i) {
        if (_routes[i].used) {
            continue;
        }

        _routes[i].used = true;
        _routes[i].method = method;
        _routes[i].handler = handler;
        copyText(path, _routes[i].path, sizeof(_routes[i].path));
        return true;
    }

    ESP32BASE_LOG_W("BaseWeb", "route table full %u/%u",
                    static_cast<unsigned>(routeCount()),
                    static_cast<unsigned>(routeCapacity()));
    return false;
}

void Esp32BaseWeb::registerBuiltInRoutes() {
    g_server.on("/", HTTP_GET, handleIndex);
    g_server.on("/esp32base", HTTP_GET, handleIndex);
    g_server.on("/esp32base/", HTTP_GET, handleIndex);
    g_server.on("/esp32base/api/status", HTTP_GET, handleStatusApi);
    g_server.on("/esp32base/wifi", HTTP_GET, handleWifiPage);
    g_server.on("/esp32base/wifi", HTTP_POST, handleWifiPost);
    g_server.on("/esp32base/api/wifi", HTTP_POST, handleWifiApi);
    g_server.on("/esp32base/ota", HTTP_GET, handleOtaPage);
    g_server.on("/esp32base/ota", HTTP_POST, handleOtaPost, handleOtaUpload);
    g_server.on("/esp32base/reboot", HTTP_POST, handleReboot);
    g_server.on("/esp32base/api/reboot", HTTP_POST, handleReboot);
}

bool Esp32BaseWeb::dispatchCustomRoute() {
    String uri = g_server.uri();
    for (uint8_t i = 0; i < routeCapacity(); ++i) {
        if (!_routes[i].used || uri != _routes[i].path || !methodMatches(_routes[i].method)) {
            continue;
        }

        if (!ensureAuthorized()) {
            return true;
        }
        _routes[i].handler();
        return true;
    }
    return false;
}

bool Esp32BaseWeb::ensureAuthorized() {
    if (!_authEnabled) {
        return true;
    }

    if (g_server.authenticate(_authUser, _authPass)) {
        return true;
    }

    g_server.requestAuthentication(BASIC_AUTH, "Esp32Base");
    return false;
}

void Esp32BaseWeb::copyText(const char* in, char* out, size_t len) {
    if (out == nullptr || len == 0U) {
        return;
    }

    const char* text = in == nullptr ? "" : in;
    strncpy(out, text, len - 1U);
    out[len - 1U] = '\0';
}

bool Esp32BaseWeb::methodMatches(Method routeMethod) {
    if (routeMethod == METHOD_ANY) {
        return true;
    }
    if (routeMethod == METHOD_GET && g_server.method() == HTTP_GET) {
        return true;
    }
    if (routeMethod == METHOD_POST && g_server.method() == HTTP_POST) {
        return true;
    }
    return false;
}

void Esp32BaseWeb::handleIndex() {
    if (g_server.uri() == "/" && dispatchCustomRoute()) {
        return;
    }

    if (!ensureAuthorized()) {
        return;
    }
    g_server.send_P(200, "text/html; charset=utf-8", kIndexHtml);
}

void Esp32BaseWeb::handleStatusApi() {
    if (!ensureAuthorized()) {
        return;
    }

    char json[384];
    snprintf(json, sizeof(json),
             "{\"ok\":true,\"hostname\":\"%s\",\"fw\":\"%s\",\"version\":\"%s\","
             "\"heap_free\":%lu,\"heap_total\":%lu,\"flash\":%lu,"
             "\"wifi\":\"%s\",\"ssid\":\"%s\",\"ip\":\"%s\",\"rssi\":%ld,"
             "\"ntp\":%s,\"mdns\":%s,\"routes\":%u}",
             Esp32Base::hostname(),
             Esp32Base::firmwareName(),
             Esp32Base::firmwareVersion(),
             static_cast<unsigned long>(Esp32BaseRuntime::freeHeap()),
             static_cast<unsigned long>(Esp32BaseRuntime::totalHeap()),
             static_cast<unsigned long>(Esp32BaseRuntime::flashSize()),
             Esp32BaseNetwork::wifiStateName(),
             Esp32BaseNetwork::ssid(),
             Esp32BaseNetwork::ip(),
             static_cast<long>(Esp32BaseNetwork::rssi()),
             Esp32BaseNetwork::isTimeSynced() ? "true" : "false",
             Esp32BaseNetwork::isMDNSRunning() ? "true" : "false",
             static_cast<unsigned>(routeCount()));
    sendJson(200, json);
}

void Esp32BaseWeb::handleWifiPage() {
    if (!ensureAuthorized()) {
        return;
    }
    g_server.send_P(200, "text/html; charset=utf-8", kWifiHtml);
}

void Esp32BaseWeb::handleWifiPost() {
    handleWifiApi();
}

void Esp32BaseWeb::handleWifiApi() {
    if (!ensureAuthorized()) {
        return;
    }

    char ssid[ESP32BASE_WIFI_SSID_LEN] = "";
    char pass[ESP32BASE_WIFI_PASS_LEN] = "";
    getParam("ssid", ssid, sizeof(ssid));
    getParam("pass", pass, sizeof(pass));

    if (ssid[0] == '\0') {
        sendJson(400, "{\"ok\":false,\"error\":\"missing_ssid\"}");
        return;
    }

    bool ok = Esp32BaseNetwork::connect(ssid, pass);
    sendJson(ok ? 200 : 500, ok ? "{\"ok\":true}" : "{\"ok\":false}");
}

void Esp32BaseWeb::handleOtaPage() {
    if (!ensureAuthorized()) {
        return;
    }
    g_server.send_P(200, "text/html; charset=utf-8", kOtaHtml);
}

void Esp32BaseWeb::handleOtaPost() {
    if (!ensureAuthorized()) {
        return;
    }

    sendText(g_otaOk ? 200 : 500, g_otaOk ? "OTA OK. Rebooting." : "OTA failed.");
    if (g_otaOk) {
        delay(200);
        Esp32BaseRuntime::restart();
    }
}

void Esp32BaseWeb::handleOtaUpload() {
    if (!ensureAuthorized()) {
        return;
    }

    HTTPUpload& upload = g_server.upload();
    if (upload.status == UPLOAD_FILE_START) {
        g_otaOk = Update.begin(UPDATE_SIZE_UNKNOWN);
        ESP32BASE_LOG_I("BaseWeb", "ota start");
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (g_otaOk && Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
            g_otaOk = false;
        }
    } else if (upload.status == UPLOAD_FILE_END) {
        if (g_otaOk) {
            g_otaOk = Update.end(true);
        } else {
            Update.end();
        }
        ESP32BASE_LOG_I("BaseWeb", "ota end ok=%u size=%lu",
                        g_otaOk ? 1U : 0U,
                        static_cast<unsigned long>(upload.totalSize));
    } else if (upload.status == UPLOAD_FILE_ABORTED) {
        Update.end();
        g_otaOk = false;
        ESP32BASE_LOG_W("BaseWeb", "ota aborted");
    }
}

void Esp32BaseWeb::handleReboot() {
    if (!ensureAuthorized()) {
        return;
    }
    sendJson(200, "{\"ok\":true,\"rebooting\":true}");
    delay(100);
    Esp32BaseRuntime::restart();
}

void Esp32BaseWeb::handleNotFound() {
    if (dispatchCustomRoute()) {
        return;
    }
    send404();
}
