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
#include "../../runtime/Esp32BaseAppEventLog.h"
#endif
#if ESP32BASE_ENABLE_FS
#include "../../runtime/Esp32BaseFs.h"
#endif
#if ESP32BASE_ENABLE_NTP
#include "../../network/Esp32BaseNtp.h"
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
#include <mbedtls/base64.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

namespace esp32base_web {

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
    WebServer server;
    Route routes[ESP32BASE_WEB_MAX_ROUTES];
    NavItem navItems[ESP32BASE_WEB_MAX_NAV_ITEMS];
    const char* headerKeys[7];
    bool webReady;
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
    bool fsUploadAppEventsTarget;
    bool fsUploadFileLogTarget;
    bool fsUploadActive;
    bool fsUploadOverwrite;
    size_t fsUploadBytes;
    char fsUploadPath[96];
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
extern const char* (&g_headerKeys)[7];
extern bool& g_webReady;
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
extern bool& g_fsUploadAppEventsTarget;
extern bool& g_fsUploadFileLogTarget;
extern bool& g_fsUploadActive;
extern bool& g_fsUploadOverwrite;
extern size_t& g_fsUploadBytes;
extern char (&g_fsUploadPath)[96];
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
extern Esp32BaseAppConfig::PageValidateCallback& g_appConfigPageValidate;
extern Esp32BaseAppConfig::ChangeCallback& g_appConfigChangeCallback;
extern Esp32BaseAppConfig::SaveCallback& g_appConfigSaveCallback;
#endif

} // namespace esp32base_web

#endif
