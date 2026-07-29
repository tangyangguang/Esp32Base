#include "../../Esp32BaseProfile.h"

#if ESP32BASE_ENABLE_WEB

#include "WebInternal.h"
#include "WebRequestPreflight.h"

#include <limits.h>

namespace esp32base_web {

enum HttpLineReadResult : uint8_t {
    HTTP_LINE_OK,
    HTTP_LINE_TOO_LONG,
    HTTP_LINE_TIMEOUT,
    HTTP_LINE_NO_MEMORY
};

static HttpLineReadResult readHttpLine(WiFiClient& client, String& line, size_t maxLength,
                                       uint32_t requestStartedMs) {
    line.remove(0);
    line.reserve(maxLength < 128U ? maxLength : 128U);
    const uint32_t timeoutMs =
        static_cast<uint32_t>(ESP32BASE_WEB_REQUEST_READ_TIMEOUT_SEC) * 1000UL;
    while (true) {
        if (!client.available()) {
            if (!client.connected() || millis() - requestStartedMs >= timeoutMs) {
                return HTTP_LINE_TIMEOUT;
            }
            delay(1);
            continue;
        }
        const int value = client.read();
        if (value < 0) {
            continue;
        }
        const char c = static_cast<char>(value);
        if (c == '\n') {
            return HTTP_LINE_OK;
        }
        if (c == '\r') {
            continue;
        }
        if (line.length() >= maxLength) {
            return HTTP_LINE_TOO_LONG;
        }
        if (!line.concat(c)) {
            return HTTP_LINE_NO_MEMORY;
        }
    }
}

static void rejectHttpRequest(WiFiClient& client, int status, const char* reason,
                              const char* extraHeaders = nullptr) {
    char response[192];
    snprintf(response, sizeof(response),
             "HTTP/1.1 %d %s\r\nConnection: close\r\n%sContent-Length: 0\r\n\r\n",
             status,
             reason ? reason : "Bad Request",
             extraHeaders ? extraHeaders : "");
    client.print(response);
}

static bool parseContentLength(const String& value, int& contentLength) {
    if (value.length() == 0) {
        return false;
    }
    uint64_t parsed = 0;
    for (size_t i = 0; i < value.length(); ++i) {
        const char c = value[i];
        if (c < '0' || c > '9') {
            return false;
        }
        parsed = parsed * 10ULL + static_cast<uint8_t>(c - '0');
        if (parsed > static_cast<uint64_t>(INT_MAX)) {
            return false;
        }
    }
    contentLength = static_cast<int>(parsed);
    return true;
}

static size_t ordinaryBodyLimit(const String& uri) {
    size_t limit = ESP32BASE_WEB_MAX_BODY_BYTES;
#if ESP32BASE_ENABLE_APP_CONFIG
    if (uri == F("/esp32base/app-config")) {
        const size_t appConfigLimit =
            64U + static_cast<size_t>(ctx().appConfigFieldCount) *
                       (Esp32BaseAppConfig::STRING_MAX_LENGTH * 3U + 16U);
        if (appConfigLimit > limit) {
            limit = appConfigLimit;
        }
    }
#else
    (void)uri;
#endif
    return limit;
}

static char* readClientBytesWithTimeout(WiFiClient& client, size_t expectedLength,
                                        size_t& dataLength, int timeoutMs) {
    dataLength = 0;
    if (expectedLength == 0 || expectedLength == SIZE_MAX) {
        return nullptr;
    }
    char* buf = static_cast<char*>(malloc(expectedLength + 1U));
    if (!buf) {
        return nullptr;
    }
    while (dataLength < expectedLength) {
        int tries = timeoutMs;
        size_t newLength = 0;
        while (!(newLength = client.available()) && tries--) {
            delay(1);
        }
        if (!newLength) {
            break;
        }
        const size_t remaining = expectedLength - dataLength;
        if (newLength > remaining) {
            newLength = remaining;
        }
        const size_t readLength = client.readBytes(buf + dataLength, newLength);
        if (readLength == 0) {
            break;
        }
        dataLength += readLength;
    }
    buf[dataLength] = '\0';
    return buf;
}

static HTTPMethod parseHttpMethod(const String& methodStr) {
#define ESP32BASE_WEB_HTTP_METHOD(num, name, string) \
    if (methodStr == F(#string)) {                   \
        return HTTP_##name;                          \
    }
    HTTP_METHOD_MAP(ESP32BASE_WEB_HTTP_METHOD)
#undef ESP32BASE_WEB_HTTP_METHOD
    return HTTP_ANY;
}

static bool requestMayHaveBody(HTTPMethod method) {
    return method == HTTP_POST || method == HTTP_PUT || method == HTTP_PATCH || method == HTTP_DELETE;
}

void Esp32BaseWebServer::handleClient() {
    if (_currentStatus == HC_NONE) {
        _currentClient = _server.available();
        if (!_currentClient) {
            if (_nullDelay) {
                delay(1);
            }
            return;
        }

        _currentClient.setTimeout(ESP32BASE_WEB_REQUEST_READ_TIMEOUT_SEC);
        _currentStatus = HC_WAIT_READ;
        _statusChange = millis();
    }

    bool keepCurrentClient = false;
    bool callYield = false;

    if (_currentClient.connected()) {
        switch (_currentStatus) {
        case HC_NONE:
            break;
        case HC_WAIT_READ:
            if (_currentClient.available()) {
                _currentClient.setTimeout(ESP32BASE_WEB_REQUEST_READ_TIMEOUT_SEC);
                if (parseRequest(_currentClient)) {
                    _currentClient.setTimeout(HTTP_MAX_SEND_WAIT / 1000);
                    _contentLength = CONTENT_LENGTH_NOT_SET;
                    _handleRequest();
                }
            } else {
                if (millis() - _statusChange <= HTTP_MAX_DATA_WAIT) {
                    keepCurrentClient = true;
                }
                callYield = true;
            }
            break;
        case HC_WAIT_CLOSE:
            if (millis() - _statusChange <= HTTP_MAX_CLOSE_WAIT) {
                keepCurrentClient = true;
                callYield = true;
            }
        }
    }

    if (!keepCurrentClient) {
        _currentClient = WiFiClient();
        _currentStatus = HC_NONE;
        _currentUpload.reset();
        _currentRaw.reset();
    }

    if (callYield) {
        yield();
    }
}

bool Esp32BaseWebServer::parseHeaders(WiFiClient& client, String& line,
                                      uint32_t requestStartedMs,
                                      bool parseBodyMetadata, String& boundary,
                                      bool& isForm, bool& isEncoded) {
    size_t headerBytes = 0;
    String headerName;
    String headerValue;
    while (true) {
        const HttpLineReadResult result =
            readHttpLine(client, line, ESP32BASE_WEB_MAX_HEADER_LINE_BYTES, requestStartedMs);
        if (result != HTTP_LINE_OK) {
            ESP32BASE_LOG_W("web", "http_header_rejected reason=%s",
                            result == HTTP_LINE_TOO_LONG
                                ? "line_too_long"
                                : (result == HTTP_LINE_NO_MEMORY ? "no_memory" : "timeout"));
            const int status = result == HTTP_LINE_TOO_LONG
                                   ? 431
                                   : (result == HTTP_LINE_NO_MEMORY ? 503 : 408);
            rejectHttpRequest(client, status,
                              result == HTTP_LINE_TOO_LONG
                                  ? "Request Header Fields Too Large"
                                  : (result == HTTP_LINE_NO_MEMORY
                                         ? "Service Unavailable"
                                         : "Request Timeout"));
            return false;
        }
        if (line == "") {
            return true;
        }
        headerBytes += line.length() + 2U;
        if (headerBytes > ESP32BASE_WEB_MAX_HEADER_BYTES) {
            ESP32BASE_LOG_W("web", "http_header_rejected reason=total_too_large");
            rejectHttpRequest(client, 431, "Request Header Fields Too Large");
            return false;
        }

        const int headerDiv = line.indexOf(':');
        if (headerDiv == -1) {
            rejectHttpRequest(client, 400, "Bad Request");
            return false;
        }
        headerName = line.substring(0, headerDiv);
        headerValue = line.substring(headerDiv + 1);
        headerValue.trim();
        _collectHeader(headerName.c_str(), headerValue.c_str());

        if (headerName.equalsIgnoreCase(F("Host"))) {
            _hostHeader = headerValue;
        } else if (parseBodyMetadata &&
                   headerName.equalsIgnoreCase(F("Content-Length"))) {
            if (!parseContentLength(headerValue, _clientContentLength)) {
                ESP32BASE_LOG_W("web", "http_body_rejected reason=invalid_content_length");
                rejectHttpRequest(client, 400, "Bad Request");
                return false;
            }
        } else if (parseBodyMetadata &&
                   headerName.equalsIgnoreCase(F("Content-Type"))) {
            if (headerValue.startsWith(F("text/plain"))) {
                isForm = false;
            } else if (headerValue.startsWith(F("application/x-www-form-urlencoded"))) {
                isForm = false;
                isEncoded = true;
            } else if (headerValue.startsWith(F("multipart/"))) {
                boundary = headerValue.substring(headerValue.indexOf('=') + 1);
                boundary.replace("\"", "");
                isForm = true;
            }
        }
    }
}

bool Esp32BaseWebServer::preflightStreamBody(WiFiClient& client, bool isForm) {
    StreamBodyKind kind = StreamBodyKind::Generic;
    bool builtInUpload = false;

#if ESP32BASE_ENABLE_OTA
    if (_currentUri == F("/esp32base/ota/raw")) {
        kind = StreamBodyKind::OtaRaw;
        builtInUpload = true;
    } else if (_currentUri == F("/esp32base/ota")) {
        kind = StreamBodyKind::OtaMultipart;
        builtInUpload = true;
    }
#endif
#if ESP32BASE_ENABLE_FS
    if (_currentUri == F("/esp32base/fs/upload")) {
        kind = StreamBodyKind::FsMultipart;
        builtInUpload = true;
    }
#endif

    if (builtInUpload) {
        if (!isAuthenticated()) {
            rejectHttpRequest(
                client, 401, "Unauthorized",
                "WWW-Authenticate: Basic realm=\"Esp32Base\"\r\n");
            return false;
        }
        if (!requestSameOrigin()) {
            ESP32BASE_LOG_W("web", "stream_upload_rejected reason=cross_origin uri=%s",
                            _currentUri.c_str());
            rejectHttpRequest(client, 403, "Forbidden");
            return false;
        }
        const bool expectsMultipart =
            kind == StreamBodyKind::OtaMultipart ||
            kind == StreamBodyKind::FsMultipart;
        if (isForm != expectsMultipart) {
            ESP32BASE_LOG_W("web", "stream_upload_rejected reason=content_type uri=%s",
                            _currentUri.c_str());
            rejectHttpRequest(client, 415, "Unsupported Media Type");
            return false;
        }
    }

    size_t declaredPayloadBytes = 0;
    size_t availablePayloadBytes = 0;
#if ESP32BASE_ENABLE_OTA
    if (kind == StreamBodyKind::OtaRaw ||
        kind == StreamBodyKind::OtaMultipart) {
        char error[96];
        if (!parseSizeHeader(
                hasHeader("X-Firmware-Size")
                    ? header("X-Firmware-Size")
                    : String(""),
                declaredPayloadBytes, error, sizeof(error))) {
            ESP32BASE_LOG_W("web", "stream_upload_rejected reason=declared_size uri=%s",
                            _currentUri.c_str());
            rejectHttpRequest(client, 400, "Bad Request");
            return false;
        }
        const esp_partition_t* target =
            esp_ota_get_next_update_partition(nullptr);
        if (!target) {
            ESP32BASE_LOG_W("web", "stream_upload_rejected reason=no_ota_partition");
            rejectHttpRequest(client, 503, "Service Unavailable");
            return false;
        }
        availablePayloadBytes = target->size;
    }
#endif
#if ESP32BASE_ENABLE_FS
    if (kind == StreamBodyKind::FsMultipart) {
        size_t total = 0;
        size_t used = 0;
        if (!Esp32BaseFs::storageInfo(total, used)) {
            ESP32BASE_LOG_W("web", "stream_upload_rejected reason=fs_unavailable");
            rejectHttpRequest(client, 503, "Service Unavailable");
            return false;
        }
        availablePayloadBytes = total > used ? total - used : 0;
    }
#endif

    const StreamBodyPreflightResult result = evaluateStreamBodyLength(
        kind,
        _clientContentLength > 0
            ? static_cast<size_t>(_clientContentLength)
            : 0,
        declaredPayloadBytes,
        availablePayloadBytes,
        ESP32BASE_WEB_MAX_STREAM_BODY_BYTES,
        ESP32BASE_WEB_MAX_UPLOAD_OVERHEAD_BYTES);
    if (result == StreamBodyPreflightResult::Allowed) {
        return true;
    }

    const int status =
        result == StreamBodyPreflightResult::LengthRequired
            ? 411
            : (result == StreamBodyPreflightResult::TooLarge ? 413 : 400);
    const char* reason =
        result == StreamBodyPreflightResult::LengthRequired
            ? "Length Required"
            : (result == StreamBodyPreflightResult::TooLarge
                   ? "Payload Too Large"
                   : "Bad Request");
    ESP32BASE_LOG_W("web",
                    "stream_upload_rejected reason=%s uri=%s bytes=%d",
                    result == StreamBodyPreflightResult::LengthRequired
                        ? "length_required"
                        : (result == StreamBodyPreflightResult::TooLarge
                               ? "too_large"
                               : "size_mismatch"),
                    _currentUri.c_str(),
                    _clientContentLength);
    rejectHttpRequest(client, status, reason);
    return false;
}

bool Esp32BaseWebServer::parseRequest(WiFiClient& client) {
    String req;
    const uint32_t requestStartedMs = millis();
    const HttpLineReadResult requestLineResult =
        readHttpLine(client, req, ESP32BASE_WEB_MAX_REQUEST_LINE_BYTES, requestStartedMs);
    if (requestLineResult != HTTP_LINE_OK) {
        ESP32BASE_LOG_W("web", "http_request_line_rejected reason=%s",
                        requestLineResult == HTTP_LINE_TOO_LONG
                            ? "too_long"
                            : (requestLineResult == HTTP_LINE_NO_MEMORY ? "no_memory" : "timeout"));
        const int status = requestLineResult == HTTP_LINE_TOO_LONG
                               ? 414
                               : (requestLineResult == HTTP_LINE_NO_MEMORY ? 503 : 408);
        rejectHttpRequest(client, status,
                          requestLineResult == HTTP_LINE_TOO_LONG
                              ? "URI Too Long"
                              : (requestLineResult == HTTP_LINE_NO_MEMORY
                                     ? "Service Unavailable"
                                     : "Request Timeout"));
        return false;
    }
    for (int i = 0; i < _headerKeysCount; ++i) {
        _currentHeaders[i].value = String();
    }

    int addrStart = req.indexOf(' ');
    int addrEnd = req.indexOf(' ', addrStart + 1);
    if (addrStart == -1 || addrEnd == -1) {
        ESP32BASE_LOG_W("web", "invalid_http_request line=%s", req.c_str());
        return false;
    }

    String methodStr = req.substring(0, addrStart);
    String url = req.substring(addrStart + 1, addrEnd);
    String versionEnd = req.substring(addrEnd + 8);
    _currentVersion = atoi(versionEnd.c_str());
    String searchStr = "";
    int hasSearch = url.indexOf('?');
    if (hasSearch != -1) {
        searchStr = url.substring(hasSearch + 1);
        url = url.substring(0, hasSearch);
    }
    _currentUri = url;
    _chunked = false;
    _clientContentLength = 0;

    HTTPMethod method = parseHttpMethod(methodStr);
    if (method == HTTP_ANY) {
        ESP32BASE_LOG_W("web", "unknown_http_method method=%s", methodStr.c_str());
        return false;
    }
    _currentMethod = method;

    RequestHandler* handler = nullptr;
    for (handler = _firstHandler; handler; handler = handler->next()) {
        if (handler->canHandle(_currentMethod, _currentUri)) {
            break;
        }
    }
    _currentHandler = handler;

    String boundaryStr;
    bool isForm = false;
    bool isEncoded = false;
    const bool mayHaveBody = requestMayHaveBody(method);
    if (!parseHeaders(client, req, requestStartedMs, mayHaveBody,
                      boundaryStr, isForm, isEncoded)) {
        return false;
    }

    if (mayHaveBody) {
        const bool streamBody = _currentHandler && _currentHandler->canRaw(_currentUri);
        const size_t bodyLimit = ordinaryBodyLimit(_currentUri);
        if (streamBody && !preflightStreamBody(client, isForm)) {
            return false;
        }
        if (!streamBody && static_cast<size_t>(_clientContentLength) > bodyLimit) {
            ESP32BASE_LOG_W("web", "http_body_rejected reason=too_large bytes=%d limit=%lu",
                            _clientContentLength,
                            static_cast<unsigned long>(bodyLimit));
            rejectHttpRequest(client, 413, "Payload Too Large");
            return false;
        }

        if (!isForm && streamBody) {
            if (!parseRawBody(client)) {
                return false;
            }
        } else if (!isForm) {
            size_t plainLength = 0;
            char* plainBuf = readClientBytesWithTimeout(client, _clientContentLength, plainLength, HTTP_MAX_POST_WAIT);
            if (_clientContentLength > 0 && !plainBuf) {
                ESP32BASE_LOG_W("web", "http_body_rejected reason=no_memory bytes=%d",
                                _clientContentLength);
                rejectHttpRequest(client, 503, "Service Unavailable");
                return false;
            }
            if (plainLength < static_cast<size_t>(_clientContentLength)) {
                free(plainBuf);
                return false;
            }
            if (_clientContentLength > 0) {
                if (isEncoded) {
                    if (searchStr != "") {
                        searchStr += '&';
                    }
                    searchStr += plainBuf;
                    free(plainBuf);
                    plainBuf = nullptr;
                }
                _parseArguments(searchStr);
                if (!isEncoded) {
                    RequestArgument& arg = _currentArgs[_currentArgCount++];
                    arg.key = F("plain");
                    arg.value = String(plainBuf);
                }
                free(plainBuf);
            } else {
                _parseArguments(searchStr);
            }
        } else {
            _parseArguments(searchStr);
            if (!_parseForm(client, boundaryStr, _clientContentLength)) {
                return false;
            }
        }
    } else {
        _parseArguments(searchStr);
    }

    client.flush();
    return true;
}

bool Esp32BaseWebServer::parseRawBody(WiFiClient& client) {
    _currentRaw.reset(new HTTPRaw());
    _currentRaw->status = RAW_START;
    _currentRaw->totalSize = 0;
    _currentRaw->currentSize = 0;
    _currentHandler->raw(*this, _currentUri, *_currentRaw);
    _currentRaw->status = RAW_WRITE;

    const size_t contentLength = _clientContentLength > 0 ? static_cast<size_t>(_clientContentLength) : 0;
    uint32_t lastProgressMs = millis();
    while (_currentRaw->totalSize < contentLength) {
        size_t available = client.available();
        if (!available) {
            if (!client.connected() || millis() - lastProgressMs > ESP32BASE_WEB_RAW_NO_PROGRESS_TIMEOUT_MS) {
                _currentRaw->status = RAW_ABORTED;
                _currentRaw->currentSize = 0;
                _currentHandler->raw(*this, _currentUri, *_currentRaw);
                return false;
            }
#if ESP32BASE_ENABLE_WATCHDOG
            Esp32BaseWatchdog::feed();
#endif
            delay(2);
            continue;
        }

        const size_t remaining = contentLength - _currentRaw->totalSize;
        size_t wanted = available;
        if (wanted > HTTP_RAW_BUFLEN) {
            wanted = HTTP_RAW_BUFLEN;
        }
        if (wanted > remaining) {
            wanted = remaining;
        }
        int readBytes = client.read(_currentRaw->buf, wanted);
        if (readBytes <= 0) {
#if ESP32BASE_ENABLE_WATCHDOG
            Esp32BaseWatchdog::feed();
#endif
            delay(2);
            continue;
        }
        _currentRaw->currentSize = static_cast<size_t>(readBytes);
        _currentRaw->totalSize += _currentRaw->currentSize;
        lastProgressMs = millis();
        _currentHandler->raw(*this, _currentUri, *_currentRaw);
    }

    _currentRaw->status = RAW_END;
    _currentRaw->currentSize = 0;
    _currentHandler->raw(*this, _currentUri, *_currentRaw);
    return true;
}

WebContext::WebContext()
    : server(80),
      routes{},
      navItems{},
      staticAssets{},
      headerKeys{"Authorization", "X-Sha256", "X-Firmware-Size", "Host", "Origin", "Referer", "X-Esp32Base-Ajax"},
      webReady(false),
      startLocked(false),
      authEnabled(true),
      defaultAuthSet(false),
      authLoadedFromStorage(false),
      defaultAuthUser{""},
      defaultAuthPass{""},
      authUser{""},
      authPass{""},
      deviceName{"Esp32Base"},
      homePath{""},
      homeMode(Esp32BaseWeb::HOME_ESP32BASE),
      systemNavMode(Esp32BaseWeb::SYSTEM_NAV_SECTION),
      footerBarMode(Esp32BaseWeb::FOOTER_BAR_FULL),
      builtinLabels{"Status", "WiFi", "OTA", "System Logs", "App Events", "System", "System", "Auth"},
      headExtraCallback(nullptr),
      pageHeadProgmem(nullptr),
      chunkBuffer{""},
      chunkUsed(0),
      activeUri{""},
      currentMethod(Esp32BaseWeb::METHOD_UNKNOWN),
      lastRequestMethod(Esp32BaseWeb::METHOD_UNKNOWN),
      requestContextActive(false),
      responseActive(false),
      responseBroken(false),
      authLoggedForRequest(false),
      otaUploadForbidden(false),
      otaUploadStartFailed(false)
#if ESP32BASE_ENABLE_FS
      ,
      fsUploadForbidden(false),
      fsUploadStartFailed(false),
      fsUploadReceived(false),
      fsUploadModified(false),
      fsUploadFileLogTarget(false),
      fsUploadActive(false),
      fsUploadOverwrite(false),
      fsUploadBytes(0),
      fsUploadPath{""},
      fsUploadTempPath{""},
      fsUploadError{""}
#endif
#if ESP32BASE_ENABLE_APP_CONFIG
      ,
      appConfigGroups{},
      appConfigFields{},
      appConfigPendingRestart{},
      appConfigGroupCount(0),
      appConfigFieldCount(0),
      appConfigRevision(1),
      appConfigTitle{"App Config"},
      appConfigSubmitContext(false),
      appConfigDefaultContext(false),
      appConfigPageValidate(nullptr),
      appConfigChangeCallback(nullptr),
      appConfigSaveCallback(nullptr)
#endif
{
}

WebContext& ctx() {
    static WebContext value;
    return value;
}

WebServer& g_server = ctx().server;
Route (&g_routes)[ESP32BASE_WEB_MAX_ROUTES] = ctx().routes;
NavItem (&g_navItems)[ESP32BASE_WEB_MAX_NAV_ITEMS] = ctx().navItems;
StaticAsset (&g_staticAssets)[ESP32BASE_WEB_MAX_STATIC_ASSETS] = ctx().staticAssets;
const char* (&g_headerKeys)[7] = ctx().headerKeys;
bool& g_webReady = ctx().webReady;
bool& g_startLocked = ctx().startLocked;
bool& g_authEnabled = ctx().authEnabled;
bool& g_defaultAuthSet = ctx().defaultAuthSet;
bool& g_authLoadedFromStorage = ctx().authLoadedFromStorage;
char (&g_defaultAuthUser)[32] = ctx().defaultAuthUser;
char (&g_defaultAuthPass)[64] = ctx().defaultAuthPass;
char (&g_authUser)[32] = ctx().authUser;
char (&g_authPass)[64] = ctx().authPass;
char (&g_deviceName)[32] = ctx().deviceName;
char (&g_homePath)[48] = ctx().homePath;
Esp32BaseWeb::HomeMode& g_homeMode = ctx().homeMode;
Esp32BaseWeb::SystemNavMode& g_systemNavMode = ctx().systemNavMode;
Esp32BaseWeb::FooterBarMode& g_footerBarMode = ctx().footerBarMode;
char (&g_builtinLabels)[8][16] = ctx().builtinLabels;
Esp32BaseWeb::Handler& g_headExtraCallback = ctx().headExtraCallback;
const char*& g_pageHeadProgmem = ctx().pageHeadProgmem;
char (&g_chunkBuffer)[512] = ctx().chunkBuffer;
size_t& g_chunkUsed = ctx().chunkUsed;
char (&g_activeUri)[48] = ctx().activeUri;
Esp32BaseWeb::Method& g_currentMethod = ctx().currentMethod;
Esp32BaseWeb::Method& g_lastRequestMethod = ctx().lastRequestMethod;
bool& g_requestContextActive = ctx().requestContextActive;
bool& g_responseActive = ctx().responseActive;
bool& g_responseBroken = ctx().responseBroken;
bool& g_authLoggedForRequest = ctx().authLoggedForRequest;
bool& g_otaUploadForbidden = ctx().otaUploadForbidden;
bool& g_otaUploadStartFailed = ctx().otaUploadStartFailed;

#if ESP32BASE_ENABLE_FS
bool& g_fsUploadForbidden = ctx().fsUploadForbidden;
bool& g_fsUploadStartFailed = ctx().fsUploadStartFailed;
bool& g_fsUploadReceived = ctx().fsUploadReceived;
bool& g_fsUploadModified = ctx().fsUploadModified;
bool& g_fsUploadFileLogTarget = ctx().fsUploadFileLogTarget;
bool& g_fsUploadActive = ctx().fsUploadActive;
bool& g_fsUploadOverwrite = ctx().fsUploadOverwrite;
size_t& g_fsUploadBytes = ctx().fsUploadBytes;
char (&g_fsUploadPath)[96] = ctx().fsUploadPath;
char (&g_fsUploadTempPath)[96] = ctx().fsUploadTempPath;
char (&g_fsUploadError)[96] = ctx().fsUploadError;
#endif

#if ESP32BASE_ENABLE_APP_CONFIG
AppConfigGroupSlot (&g_appConfigGroups)[ESP32BASE_APP_CONFIG_MAX_GROUPS] = ctx().appConfigGroups;
AppConfigFieldSlot (&g_appConfigFields)[ESP32BASE_APP_CONFIG_MAX_FIELDS] = ctx().appConfigFields;
AppConfigPendingRestartSlot (&g_appConfigPendingRestart)[ESP32BASE_APP_CONFIG_MAX_FIELDS] = ctx().appConfigPendingRestart;
uint8_t& g_appConfigGroupCount = ctx().appConfigGroupCount;
uint8_t& g_appConfigFieldCount = ctx().appConfigFieldCount;
uint32_t& g_appConfigRevision = ctx().appConfigRevision;
char (&g_appConfigTitle)[32] = ctx().appConfigTitle;
bool& g_appConfigSubmitContext = ctx().appConfigSubmitContext;
bool& g_appConfigDefaultContext = ctx().appConfigDefaultContext;
Esp32BaseAppConfig::PageValidateCallback& g_appConfigPageValidate = ctx().appConfigPageValidate;
Esp32BaseAppConfig::ChangeCallback& g_appConfigChangeCallback = ctx().appConfigChangeCallback;
Esp32BaseAppConfig::SaveCallback& g_appConfigSaveCallback = ctx().appConfigSaveCallback;
#endif

} // namespace esp32base_web

#endif
