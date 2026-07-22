#include "../../Esp32BaseProfile.h"

#if ESP32BASE_ENABLE_WEB

#include "WebContext.h"

namespace esp32base_web {

static char* readClientBytesWithTimeout(WiFiClient& client, size_t maxLength, size_t& dataLength, int timeoutMs) {
    char* buf = nullptr;
    dataLength = 0;
    while (dataLength < maxLength) {
        int tries = timeoutMs;
        size_t newLength = 0;
        while (!(newLength = client.available()) && tries--) {
            delay(1);
        }
        if (!newLength) {
            break;
        }
        const size_t remaining = maxLength - dataLength;
        if (newLength > remaining) {
            newLength = remaining;
        }
        char* newBuf = static_cast<char*>(buf ? realloc(buf, dataLength + newLength + 1) : malloc(newLength + 1));
        if (!newBuf) {
            free(buf);
            return nullptr;
        }
        buf = newBuf;
        client.readBytes(buf + dataLength, newLength);
        dataLength += newLength;
        buf[dataLength] = '\0';
    }
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

bool Esp32BaseWebServer::parseRequest(WiFiClient& client) {
    String req = client.readStringUntil('\r');
    client.readStringUntil('\n');
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

    if (requestMayHaveBody(method)) {
        String boundaryStr;
        String headerName;
        String headerValue;
        bool isForm = false;
        bool isEncoded = false;
        while (true) {
            req = client.readStringUntil('\r');
            client.readStringUntil('\n');
            if (req == "") {
                break;
            }
            int headerDiv = req.indexOf(':');
            if (headerDiv == -1) {
                break;
            }
            headerName = req.substring(0, headerDiv);
            headerValue = req.substring(headerDiv + 1);
            headerValue.trim();
            _collectHeader(headerName.c_str(), headerValue.c_str());

            if (headerName.equalsIgnoreCase(F("Content-Type"))) {
                if (headerValue.startsWith(F("text/plain"))) {
                    isForm = false;
                } else if (headerValue.startsWith(F("application/x-www-form-urlencoded"))) {
                    isForm = false;
                    isEncoded = true;
                } else if (headerValue.startsWith(F("multipart/"))) {
                    boundaryStr = headerValue.substring(headerValue.indexOf('=') + 1);
                    boundaryStr.replace("\"", "");
                    isForm = true;
                }
            } else if (headerName.equalsIgnoreCase(F("Content-Length"))) {
                _clientContentLength = headerValue.toInt();
            } else if (headerName.equalsIgnoreCase(F("Host"))) {
                _hostHeader = headerValue;
            }
        }

        if (!isForm && _currentHandler && _currentHandler->canRaw(_currentUri)) {
            if (!parseRawBody(client)) {
                return false;
            }
        } else if (!isForm) {
            size_t plainLength = 0;
            char* plainBuf = readClientBytesWithTimeout(client, _clientContentLength, plainLength, HTTP_MAX_POST_WAIT);
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
        String headerName;
        String headerValue;
        while (true) {
            req = client.readStringUntil('\r');
            client.readStringUntil('\n');
            if (req == "") {
                break;
            }
            int headerDiv = req.indexOf(':');
            if (headerDiv == -1) {
                break;
            }
            headerName = req.substring(0, headerDiv);
            headerValue = req.substring(headerDiv + 2);
            _collectHeader(headerName.c_str(), headerValue.c_str());

            if (headerName.equalsIgnoreCase(F("Host"))) {
                _hostHeader = headerValue;
            }
        }
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
