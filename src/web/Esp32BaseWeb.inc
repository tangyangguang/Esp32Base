#include "../Esp32BaseProfile.h"

#if ESP32BASE_ENABLE_WEB

#include "Esp32BaseWeb.h"
#include "../Esp32Base.h"
#include "../core/Esp32BaseLog.h"
#include "../core/Esp32BaseSystem.h"
#include "../core/Esp32BaseConfig.h"
#include "../network/Esp32BaseWiFi.h"

#if ESP32BASE_ENABLE_FILELOG
#include "../runtime/Esp32BaseFileLog.h"
#endif

#if ESP32BASE_ENABLE_BUS
#include "../runtime/Esp32BaseBus.h"
#endif
#if ESP32BASE_ENABLE_OTA
#include "../update/Esp32BaseOta.h"
#endif

#define ESP32BASE_INCLUDE_WEBSERVER <WebServer.h>
#include ESP32BASE_INCLUDE_WEBSERVER
#undef ESP32BASE_INCLUDE_WEBSERVER
#include <stdlib.h>
#include <string.h>

namespace {
struct Route {
    char path[48];
    Esp32BaseWeb::Method method;
    Esp32BaseWeb::Handler handler;
    bool registered;
};

WebServer g_server(80);
Route g_routes[ESP32BASE_WEB_MAX_ROUTES];
const char* g_headerKeys[] = {"X-Sha256", "X-Firmware-Size"};
bool g_webReady = false;
bool g_authEnabled = true;
bool g_authSetByApp = false;
char g_authUser[32] = "admin";
char g_authPass[32] = "admin";
String g_webJson;
int g_webJsonCode = 200;
char g_chunkBuffer[512];
size_t g_chunkUsed = 0;
char g_activeUri[48] = "";
char g_activeMethod[5] = "";

static const char WEB_HEAD[] PROGMEM =
    "<meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<style>"
    "body{font-family:sans-serif;padding:12px;max-width:760px;margin:auto}"
    "h1,h2{margin:0 0 12px}"
    "a,button,input[type=submit],input[type=button]{background:#2678c8;color:#fff;padding:7px 12px;text-decoration:none;border:0;border-radius:3px;cursor:pointer;margin:2px 2px 2px 0}"
    "input:not([type=submit]):not([type=button]){width:100%;padding:7px;margin:4px 0 12px;border:1px solid #ccc;border-radius:3px;box-sizing:border-box}"
    "nav{margin-bottom:14px;border-bottom:1px solid #eee;padding-bottom:8px}"
    "nav a{font-size:.88em}.ok{color:green}.err{color:#c33}.info{color:#777;font-size:.86em;margin-top:16px}"
    "pre{white-space:pre;overflow-x:auto;word-break:normal;font-size:.82em;background:#f7f7f7;padding:8px}"
    "</style>"
    "<script>function once(f){if(f.dataset.busy)return false;f.dataset.busy=1;var b=f.querySelector('[type=submit]');if(b)b.disabled=true;return true;}</script>";

void flushChunkBuffer();
void markRequest();

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
    if (g_server.authenticate(g_authUser, g_authPass)) {
        return true;
    }
    g_server.requestAuthentication();
    return false;
}

bool isAuthenticated() {
    return !g_authEnabled || g_server.authenticate(g_authUser, g_authPass);
}

void registerRoute(Route& route) {
    g_server.on(route.path, toHttpMethod(route.method), route.handler);
    route.registered = true;
}

void beginChunked(int code, const char* contentType) {
    g_chunkUsed = 0;
    g_server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    g_server.send(code, contentType, "");
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

void endChunked() {
    flushChunkBuffer();
    g_server.sendContent("");
}

void markRequest() {
    strlcpy(g_activeMethod, g_server.method() == HTTP_POST ? "POST" : "GET", sizeof(g_activeMethod));
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
    beginChunked(200, "application/json");
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
    sendChunk(",\"wifi\":{\"state\":\"");
    sendEscapedJsonChunk(Esp32BaseWiFi::stateName());
    sendChunk("\",\"connected\":");
    sendChunk(Esp32BaseWiFi::isConnected() ? "true" : "false");
    sendChunk("}}");
    endChunked();
}

void handleChip() {
    markRequest();
    if (!ensureAuth()) {
        return;
    }
    beginChunked(200, "application/json");
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
    sendChunk("\",\"wakeReason\":\"");
    sendEscapedJsonChunk(Esp32BaseSystem::wakeReason());
    sendChunk("\"}");
    endChunked();
}

void handleFirmware() {
    markRequest();
    if (!ensureAuth()) {
        return;
    }
    beginChunked(200, "application/json");
    sendChunk("{\"name\":\"");
    sendEscapedJsonChunk(Esp32Base::firmwareName());
    sendChunk("\",\"version\":\"");
    sendEscapedJsonChunk(Esp32Base::firmwareVersion());
    sendChunk("\",\"build\":\"");
    sendEscapedJsonChunk(Esp32Base::firmwareBuild());
    sendChunk("\"}");
    endChunked();
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

    Esp32BaseWeb::sendHeader("WiFi");
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
    ESP32BASE_LOG_I("web", "wifi form submitted ssid=%s password_length=%u result=%s",
                    ssid.c_str(), static_cast<unsigned>(password.length()), ok ? "success" : "failed");
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

void handleRebootPage() {
    markRequest();
    if (!ensureAuth()) {
        return;
    }
    Esp32BaseWeb::sendHeader("Reboot");
    sendChunk("<h2>Reboot</h2><p>Are you sure you want to reboot the device?</p><form method='post' action='/esp32base/reboot' onsubmit=\"return confirm('Reboot device now?')&&once(this)\"><input type='submit' value='Confirm Reboot' style='background:#c33'> <a href='/esp32base'>Cancel</a></form>");
    Esp32BaseWeb::sendFooter();
}

void handleRebootPost() {
    markRequest();
    if (!ensureAuth()) {
        return;
    }
    Esp32BaseWeb::sendHeader("Rebooting");
    sendChunk("<h2>Rebooting...</h2><p>Device is restarting. Please wait a few seconds, then reload.</p>");
    Esp32BaseWeb::sendFooter();
    delay(100);
    Esp32BaseSystem::restart("web");
}

void handleCaptiveProbe() {
    g_server.sendHeader("Location", "/esp32base/wifi", true);
    g_server.send(302, "text/plain", "");
}

void handleRootRedirect() {
    g_server.sendHeader("Location", "/esp32base", true);
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
    Esp32BaseWeb::sendHeader("OTA");
    sendChunk("<h2>OTA Update</h2><form id='f'><input id='fw' type='file' name='firmware' accept='.bin' required><input id='sha' placeholder='SHA256 optional' maxlength='64'><input type='submit' value='Upload Firmware'></form><progress id='p' value='0' max='100' style='width:100%;display:none'></progress><p id='s'></p>");
    sendChunk("<script>function h(n){var u=['B','KB','MB','GB'],i=0,x=n;while(x>=1024&&i<u.length-1){x/=1024;i++;}return n+' bytes ('+(i?x.toFixed(2):x)+' '+u[i]+')';}document.getElementById('f').onsubmit=function(e){e.preventDefault();if(this.dataset.busy)return false;this.dataset.busy=1;var f=document.getElementById('fw').files[0],st=document.getElementById('s'),pg=document.getElementById('p'),b=this.querySelector('[type=submit]');if(!f){this.dataset.busy='';return false;}var d=new FormData();d.append('firmware',f,f.name);var x=new XMLHttpRequest();if(b)b.disabled=true;pg.style.display='block';st.textContent='Uploading 0%';x.upload.onprogress=function(ev){if(ev.lengthComputable){var p=Math.floor(ev.loaded*100/ev.total);pg.value=p;st.textContent='Uploading '+p+'% '+h(ev.loaded)+' / '+h(ev.total);}};x.onload=function(){st.textContent=(x.status==200?x.responseText:('Upload failed: HTTP '+x.status+' '+x.responseText));if(x.status!=200){document.getElementById('f').dataset.busy='';if(b)b.disabled=false;}};x.onerror=function(){st.textContent='Upload failed: network error';document.getElementById('f').dataset.busy='';if(b)b.disabled=false;};x.open('POST','/esp32base/ota');x.setRequestHeader('X-Firmware-Size',String(f.size));var s=document.getElementById('sha').value;if(s)x.setRequestHeader('X-Sha256',s);x.send(d);return false;};</script>");
    Esp32BaseWeb::sendFooter();
}

void handleOtaUploadDone() {
    markRequest();
    if (!ensureAuth()) {
        return;
    }
    const bool ok = Esp32BaseOta::status() == Esp32BaseOta::SUCCESS;
    g_server.send(ok ? 200 : 500, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
    if (ok) {
        delay(100);
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
    beginChunked(200, "application/json");
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
    endChunked();
}
#endif

void handleRoot() {
    markRequest();
    if (!ensureAuth()) {
        return;
    }
    Esp32BaseWeb::sendHeader("Home");
    sendChunk("<h2>Esp32Base</h2><p>Ready</p>");
    Esp32BaseWeb::sendFooter();
}

#if ESP32BASE_ENABLE_FILELOG
void sendLogEscapedChunk(const char* data, size_t len, void*) {
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
#endif

void handleLogsPage() {
    markRequest();
    if (!ensureAuth()) {
        return;
    }
    Esp32BaseWeb::sendHeader("Logs");
    sendChunk("<h2>Logs</h2>");
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
    Esp32BaseLog::formatBytes(Esp32BaseFileLog::maxBytes(), maxBuf, sizeof(maxBuf));
    Esp32BaseLog::formatBytes(static_cast<uint64_t>(Esp32BaseFileLog::maxBytes()) * Esp32BaseFileLog::rotateFiles(), totalBuf, sizeof(totalBuf));
    char line[192];
    snprintf(line, sizeof(line), "<p>File log: <b>enabled</b><br>Path: ");
    sendChunk(line);
    sendEscapedHtmlChunk(Esp32BaseFileLog::path());
    snprintf(line, sizeof(line), "<br>Rotation files: %u<br>File level: %s<br>Buffer: %u / %u<br>Flush interval: %lu ms<br>Max per file: %s<br>Max total: %s<br>Segments: ",
             static_cast<unsigned>(Esp32BaseFileLog::rotateFiles()),
             Esp32BaseFileLog::levelName(),
             static_cast<unsigned>(Esp32BaseFileLog::bufferUsed()),
             static_cast<unsigned>(Esp32BaseFileLog::bufferSize()),
             static_cast<unsigned long>(Esp32BaseFileLog::flushIntervalMs()),
             maxBuf,
             totalBuf);
    sendChunk(line);
    for (int8_t i = static_cast<int8_t>(Esp32BaseFileLog::rotateFiles()) - 1; i >= 0; --i) {
        char sizeBuf[48];
        Esp32BaseLog::formatBytes(Esp32BaseFileLog::segmentSize(static_cast<uint8_t>(i)), sizeBuf, sizeof(sizeBuf));
        snprintf(line, sizeof(line), "%s%u=%s", i == static_cast<int8_t>(Esp32BaseFileLog::rotateFiles()) - 1 ? "" : ", ", static_cast<unsigned>(i), sizeBuf);
        sendChunk(line);
    }
    sendChunk("</p><pre>");
    for (int8_t i = static_cast<int8_t>(Esp32BaseFileLog::rotateFiles()) - 1; i >= 1; --i) {
        snprintf(line, sizeof(line), "\n\n----- history-%u -----\n", static_cast<unsigned>(i));
        sendChunk(line);
        Esp32BaseFileLog::streamSegment(static_cast<uint8_t>(i), sendLogEscapedChunk);
    }
    sendChunk("\n\n----- current-0 -----\n");
    Esp32BaseFileLog::streamSegment(0, sendLogEscapedChunk);
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
}

bool Esp32BaseWeb::begin() {
    if (g_webReady) {
        return true;
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
    g_server.on("/esp32base/reboot", HTTP_GET, handleRebootPage);
    g_server.on("/esp32base/reboot", HTTP_POST, handleRebootPost);
    g_server.on("/esp32base/api/restart", HTTP_POST, handleRestart);
    g_server.on("/generate_204", HTTP_GET, handleCaptiveProbe);
    g_server.on("/hotspot-detect.html", HTTP_GET, handleCaptiveProbe);
    g_server.on("/ncsi.txt", HTTP_GET, handleCaptiveProbe);
    g_server.on("/favicon.ico", HTTP_GET, handleNoContent);
#if ESP32BASE_ENABLE_OTA
    if (isAuthSetByApplication()) {
        g_server.on("/esp32base/ota", HTTP_GET, handleOtaPage);
        g_server.on("/esp32base/ota", HTTP_POST, handleOtaUploadDone, handleOtaUpload);
        g_server.on("/esp32base/api/ota", HTTP_GET, handleOtaApi);
    } else {
        ESP32BASE_LOG_E("web", "OTA route not registered: application auth not set");
    }
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
        g_activeMethod[0] = '\0';
        const uint32_t start = millis();
        g_server.handleClient();
        const uint32_t elapsed = millis() - start;
        if (elapsed > 250UL) {
            ESP32BASE_LOG_W("web", "slow_request method=%s uri=%s elapsed=%lu ms",
                            g_activeMethod[0] ? g_activeMethod : "?",
                            g_activeUri[0] ? g_activeUri : "(unknown)",
                            static_cast<unsigned long>(elapsed));
        }
    }
}

bool Esp32BaseWeb::isReady() {
    return g_webReady;
}

void Esp32BaseWeb::setAuth(const char* user, const char* pass) {
    if (user && user[0]) {
        strlcpy(g_authUser, user, sizeof(g_authUser));
    }
    if (pass && pass[0]) {
        strlcpy(g_authPass, pass, sizeof(g_authPass));
    }
    g_authSetByApp = true;
}

bool Esp32BaseWeb::isAuthEnabled() {
    return g_authEnabled;
}

bool Esp32BaseWeb::isAuthSetByApplication() {
    return g_authSetByApp;
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

bool Esp32BaseWeb::addRoute(const char* path, Method method, Handler handler) {
    if (!path || path[0] != '/' || !handler || strlen(path) >= sizeof(g_routes[0].path)) {
        return false;
    }
    for (uint8_t i = 0; i < ESP32BASE_WEB_MAX_ROUTES; ++i) {
        if (!g_routes[i].handler) {
            strlcpy(g_routes[i].path, path, sizeof(g_routes[i].path));
            g_routes[i].method = method;
            g_routes[i].handler = handler;
            if (g_webReady) {
                registerRoute(g_routes[i]);
            }
            return true;
        }
    }
    return false;
}

bool Esp32BaseWeb::addPage(const char* path, Handler handler) {
    return addRoute(path, METHOD_GET, handler);
}

bool Esp32BaseWeb::addApi(const char* path, Handler handler) {
    return addRoute(path, METHOD_ANY, handler);
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
    beginChunked(200, "text/html; charset=utf-8");
    sendChunk("<!doctype html><html><head><title>");
    sendEscapedHtmlChunk(title ? title : "Esp32Base");
    sendChunk("</title>");
    sendProgmem(WEB_HEAD);
    sendChunk("</head><body><nav><a href='/esp32base'>Home</a><a href='/esp32base/wifi'>WiFi</a>");
#if ESP32BASE_ENABLE_OTA
    sendChunk("<a href='/esp32base/ota'>OTA</a>");
#endif
    sendChunk("<a href='/esp32base/logs'>Logs</a><a href='/esp32base/reboot'>Reboot</a></nav>");
}

void Esp32BaseWeb::sendFooter() {
    char heap[48];
    formatReadableBytes(Esp32BaseSystem::freeHeap(), heap, sizeof(heap));
    sendChunk("<div class=info>Free heap: ");
    sendEscapedHtmlChunk(heap);
    sendChunk("</div></body></html>");
    endChunked();
}

void Esp32BaseWeb::sendChunk(const char* text) {
    ::sendChunk(text);
}

void Esp32BaseWeb::writeHtmlEscaped(const char* text) {
    sendEscapedHtmlChunk(text);
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
    g_webJsonCode = code;
    g_webJson = "{";
}

void Esp32BaseWeb::writeJsonEscaped(const char* text) {
    if (!text) {
        return;
    }
    for (const char* p = text; *p; ++p) {
        switch (*p) {
            case '"': g_webJson += "\\\""; break;
            case '\\': g_webJson += "\\\\"; break;
            case '\n': g_webJson += "\\n"; break;
            case '\r': g_webJson += "\\r"; break;
            case '\t': g_webJson += "\\t"; break;
            default:
                if (static_cast<uint8_t>(*p) < 0x20) {
                    char buf[7];
                    snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned>(*p));
                    g_webJson += buf;
                } else {
                    g_webJson += *p;
                }
                break;
        }
    }
}

void Esp32BaseWeb::endJson() {
    g_webJson += "}";
    g_server.send(g_webJsonCode, "application/json", g_webJson);
    g_webJson = "";
    g_webJsonCode = 200;
}

#endif
