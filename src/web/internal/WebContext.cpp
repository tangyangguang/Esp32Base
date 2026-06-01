#include "../../Esp32BaseProfile.h"

#if ESP32BASE_ENABLE_WEB

#include "WebContext.h"

namespace esp32base_web {

WebContext::WebContext()
    : server(80),
      routes{},
      navItems{},
      headerKeys{"Authorization", "X-Sha256", "X-Firmware-Size", "Host", "Origin", "Referer", "X-Esp32Base-Ajax"},
      webReady(false),
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
      fsUploadAppEventsTarget(false),
      fsUploadFileLogTarget(false),
      fsUploadActive(false),
      fsUploadOverwrite(false),
      fsUploadBytes(0),
      fsUploadPath{""},
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
const char* (&g_headerKeys)[7] = ctx().headerKeys;
bool& g_webReady = ctx().webReady;
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
bool& g_fsUploadAppEventsTarget = ctx().fsUploadAppEventsTarget;
bool& g_fsUploadFileLogTarget = ctx().fsUploadFileLogTarget;
bool& g_fsUploadActive = ctx().fsUploadActive;
bool& g_fsUploadOverwrite = ctx().fsUploadOverwrite;
size_t& g_fsUploadBytes = ctx().fsUploadBytes;
char (&g_fsUploadPath)[96] = ctx().fsUploadPath;
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
Esp32BaseAppConfig::PageValidateCallback& g_appConfigPageValidate = ctx().appConfigPageValidate;
Esp32BaseAppConfig::ChangeCallback& g_appConfigChangeCallback = ctx().appConfigChangeCallback;
Esp32BaseAppConfig::SaveCallback& g_appConfigSaveCallback = ctx().appConfigSaveCallback;
#endif

} // namespace esp32base_web

#endif
