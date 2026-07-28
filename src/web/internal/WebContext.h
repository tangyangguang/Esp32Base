#pragma once

#include "../../Esp32BaseProfile.h"

#if ESP32BASE_ENABLE_WEB

#include "../Esp32BaseWeb.h"
#include "../../Esp32Base.h"
#include "../../core/Esp32BaseConfig.h"
#include "../../core/Esp32BaseLog.h"
#include "../../core/Esp32BaseSystem.h"
#include "../../network/Esp32BaseWiFi.h"

#if ESP32BASE_ENABLE_APP_CONFIG
#include "../Esp32BaseAppConfig.h"
#endif
#if ESP32BASE_ENABLE_BUS
#include "../../runtime/Esp32BaseBus.h"
#endif
#if ESP32BASE_ENABLE_FILELOG
#include "../../runtime/Esp32BaseFileLog.h"
#endif
#if ESP32BASE_ENABLE_APP_EVENTS
#include "../../runtime/Esp32BaseAppEvents.h"
#endif
#if ESP32BASE_ENABLE_RECORD_STORE
#include "../../runtime/Esp32BaseRecordStore.h"
#endif
#if ESP32BASE_ENABLE_FS
#include "../../runtime/Esp32BaseFs.h"
#endif
#if ESP32BASE_ENABLE_NTP
#include "../../network/Esp32BaseNtp.h"
#endif
#if ESP32BASE_ENABLE_MDNS
#include "../../network/Esp32BaseMdns.h"
#endif
#if ESP32BASE_ENABLE_HEALTH
#include "../../runtime/Esp32BaseHealth.h"
#endif
#if ESP32BASE_ENABLE_TIME
#include "../../runtime/Esp32BaseTime.h"
#endif
#if ESP32BASE_ENABLE_RTC
#include "../../runtime/Esp32BaseRtc.h"
#endif
#if ESP32BASE_ENABLE_OTA
#include "../../update/Esp32BaseOta.h"
#endif
#if ESP32BASE_ENABLE_WATCHDOG
#include "../../runtime/Esp32BaseWatchdog.h"
#endif

#include <WebServer.h>
#include <WiFi.h>
#include <Preferences.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_image_format.h>
#include <esp_flash_encrypt.h>
#include <esp_secure_boot.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <mbedtls/base64.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef ESP32BASE_WEB_REQUEST_READ_TIMEOUT_SEC
#define ESP32BASE_WEB_REQUEST_READ_TIMEOUT_SEC 5
#endif

#ifndef ESP32BASE_WEB_RAW_NO_PROGRESS_TIMEOUT_MS
#define ESP32BASE_WEB_RAW_NO_PROGRESS_TIMEOUT_MS 30000UL
#endif

#ifndef ESP32BASE_WEB_MAX_REQUEST_LINE_BYTES
#define ESP32BASE_WEB_MAX_REQUEST_LINE_BYTES 1024U
#endif

#ifndef ESP32BASE_WEB_MAX_HEADER_LINE_BYTES
#define ESP32BASE_WEB_MAX_HEADER_LINE_BYTES 1024U
#endif

#ifndef ESP32BASE_WEB_MAX_HEADER_BYTES
#define ESP32BASE_WEB_MAX_HEADER_BYTES 8192U
#endif

#ifndef ESP32BASE_WEB_MAX_BODY_BYTES
#define ESP32BASE_WEB_MAX_BODY_BYTES 8192U
#endif

#if ESP32BASE_WEB_REQUEST_READ_TIMEOUT_SEC < 1
#error "ESP32BASE_WEB_REQUEST_READ_TIMEOUT_SEC must be at least 1"
#endif

#if ESP32BASE_WEB_RAW_NO_PROGRESS_TIMEOUT_MS < 1000UL
#error "ESP32BASE_WEB_RAW_NO_PROGRESS_TIMEOUT_MS must be at least 1000"
#endif

#if ESP32BASE_WEB_MAX_REQUEST_LINE_BYTES < 128U
#error "ESP32BASE_WEB_MAX_REQUEST_LINE_BYTES must be at least 128"
#endif

#if ESP32BASE_WEB_MAX_HEADER_LINE_BYTES < 128U
#error "ESP32BASE_WEB_MAX_HEADER_LINE_BYTES must be at least 128"
#endif

#if ESP32BASE_WEB_MAX_HEADER_BYTES < ESP32BASE_WEB_MAX_HEADER_LINE_BYTES
#error "ESP32BASE_WEB_MAX_HEADER_BYTES must cover at least one header line"
#endif

#if ESP32BASE_WEB_MAX_BODY_BYTES < 256U
#error "ESP32BASE_WEB_MAX_BODY_BYTES must be at least 256"
#endif

namespace esp32base_web {

class Esp32BaseWebServer : public WebServer {
public:
    explicit Esp32BaseWebServer(int port = 80) : WebServer(port) {}

    void handleClient() override;

private:
    bool parseRequest(WiFiClient& client);
    bool parseHeaders(WiFiClient& client, String& line, uint32_t requestStartedMs,
                      bool parseBodyMetadata, String& boundary,
                      bool& isForm, bool& isEncoded);
    bool parseRawBody(WiFiClient& client);
};

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

struct StaticAsset {
    char path[48];
    const char* contentType;
    const uint8_t* data;
    size_t len;
    uint32_t cacheMaxAgeSec;
    bool authRequired;
    bool registered;
};

#if ESP32BASE_ENABLE_FS
struct FsTopEntry {
    char path[96];
    uint64_t size;
};

struct FsScan {
    uint32_t files;
    uint32_t dirs;
    uint32_t entries;
    uint64_t listedSize;
    uint8_t topCount;
    FsTopEntry top[10];
};

struct FsWalkFrame {
    FsScan* scan;
    const char* dir;
    uint8_t depth;
    bool emitRows;
    bool manage;
    uint16_t emitLimit;
    uint16_t* emittedRows;
};

struct FsUploadDirFrame {
    const char* dir;
    uint8_t depth;
    uint16_t* emitted;
    bool* pathTooLong;
};
#endif

#if ESP32BASE_ENABLE_WATCHDOG
struct WatchdogTripState {
    uint32_t lifetime;
    uint32_t base;
    uint32_t trip;
    uint32_t resetTime;
    bool hasBase;
    bool invalidBase;
};
#endif

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
#endif

struct WebContext {
    Esp32BaseWebServer server;
    Route routes[ESP32BASE_WEB_MAX_ROUTES];
    NavItem navItems[ESP32BASE_WEB_MAX_NAV_ITEMS];
    StaticAsset staticAssets[ESP32BASE_WEB_MAX_STATIC_ASSETS];
    const char* headerKeys[7];
    bool webReady;
    bool startLocked;
    bool authEnabled;
    bool defaultAuthSet;
    bool authLoadedFromStorage;
    char defaultAuthUser[32];
    char defaultAuthPass[64];
    char authUser[32];
    char authPass[64];
    char deviceName[32];
    char homePath[48];
    Esp32BaseWeb::HomeMode homeMode;
    Esp32BaseWeb::SystemNavMode systemNavMode;
    Esp32BaseWeb::FooterBarMode footerBarMode;
    char builtinLabels[8][16];
    Esp32BaseWeb::Handler headExtraCallback;
    const char* pageHeadProgmem;
    char chunkBuffer[512];
    size_t chunkUsed;
    char activeUri[48];
    Esp32BaseWeb::Method currentMethod;
    Esp32BaseWeb::Method lastRequestMethod;
    bool requestContextActive;
    bool responseActive;
    bool responseBroken;
    bool authLoggedForRequest;
    bool otaUploadForbidden;
    bool otaUploadStartFailed;
#if ESP32BASE_ENABLE_FS
    bool fsUploadForbidden;
    bool fsUploadStartFailed;
    bool fsUploadReceived;
    bool fsUploadModified;
    bool fsUploadFileLogTarget;
    bool fsUploadActive;
    bool fsUploadOverwrite;
    size_t fsUploadBytes;
    char fsUploadPath[96];
    char fsUploadTempPath[96];
    char fsUploadError[96];
#endif
#if ESP32BASE_ENABLE_APP_CONFIG
    AppConfigGroupSlot appConfigGroups[ESP32BASE_APP_CONFIG_MAX_GROUPS];
    AppConfigFieldSlot appConfigFields[ESP32BASE_APP_CONFIG_MAX_FIELDS];
    AppConfigPendingRestartSlot appConfigPendingRestart[ESP32BASE_APP_CONFIG_MAX_FIELDS];
    uint8_t appConfigGroupCount;
    uint8_t appConfigFieldCount;
    uint32_t appConfigRevision;
    char appConfigTitle[32];
    bool appConfigSubmitContext;
    bool appConfigDefaultContext;
    Esp32BaseAppConfig::PageValidateCallback appConfigPageValidate;
    Esp32BaseAppConfig::ChangeCallback appConfigChangeCallback;
    Esp32BaseAppConfig::SaveCallback appConfigSaveCallback;
#endif

    WebContext();
};

WebContext& ctx();

extern WebServer& g_server;
extern Route (&g_routes)[ESP32BASE_WEB_MAX_ROUTES];
extern NavItem (&g_navItems)[ESP32BASE_WEB_MAX_NAV_ITEMS];
extern StaticAsset (&g_staticAssets)[ESP32BASE_WEB_MAX_STATIC_ASSETS];
extern const char* (&g_headerKeys)[7];
extern bool& g_webReady;
extern bool& g_startLocked;
extern bool& g_authEnabled;
extern bool& g_defaultAuthSet;
extern bool& g_authLoadedFromStorage;
extern char (&g_defaultAuthUser)[32];
extern char (&g_defaultAuthPass)[64];
extern char (&g_authUser)[32];
extern char (&g_authPass)[64];
extern char (&g_deviceName)[32];
extern char (&g_homePath)[48];
extern Esp32BaseWeb::HomeMode& g_homeMode;
extern Esp32BaseWeb::SystemNavMode& g_systemNavMode;
extern Esp32BaseWeb::FooterBarMode& g_footerBarMode;
extern char (&g_builtinLabels)[8][16];
extern Esp32BaseWeb::Handler& g_headExtraCallback;
extern const char*& g_pageHeadProgmem;
extern char (&g_chunkBuffer)[512];
extern size_t& g_chunkUsed;
extern char (&g_activeUri)[48];
extern Esp32BaseWeb::Method& g_currentMethod;
extern Esp32BaseWeb::Method& g_lastRequestMethod;
extern bool& g_requestContextActive;
extern bool& g_responseActive;
extern bool& g_responseBroken;
extern bool& g_authLoggedForRequest;
extern bool& g_otaUploadForbidden;
extern bool& g_otaUploadStartFailed;

#if ESP32BASE_ENABLE_FS
extern bool& g_fsUploadForbidden;
extern bool& g_fsUploadStartFailed;
extern bool& g_fsUploadReceived;
extern bool& g_fsUploadModified;
extern bool& g_fsUploadFileLogTarget;
extern bool& g_fsUploadActive;
extern bool& g_fsUploadOverwrite;
extern size_t& g_fsUploadBytes;
extern char (&g_fsUploadPath)[96];
extern char (&g_fsUploadTempPath)[96];
extern char (&g_fsUploadError)[96];
#endif

#if ESP32BASE_ENABLE_APP_CONFIG
extern AppConfigGroupSlot (&g_appConfigGroups)[ESP32BASE_APP_CONFIG_MAX_GROUPS];
extern AppConfigFieldSlot (&g_appConfigFields)[ESP32BASE_APP_CONFIG_MAX_FIELDS];
extern AppConfigPendingRestartSlot (&g_appConfigPendingRestart)[ESP32BASE_APP_CONFIG_MAX_FIELDS];
extern uint8_t& g_appConfigGroupCount;
extern uint8_t& g_appConfigFieldCount;
extern uint32_t& g_appConfigRevision;
extern char (&g_appConfigTitle)[32];
extern bool& g_appConfigSubmitContext;
extern bool& g_appConfigDefaultContext;
extern Esp32BaseAppConfig::PageValidateCallback& g_appConfigPageValidate;
extern Esp32BaseAppConfig::ChangeCallback& g_appConfigChangeCallback;
extern Esp32BaseAppConfig::SaveCallback& g_appConfigSaveCallback;
#endif

} // namespace esp32base_web

#endif
