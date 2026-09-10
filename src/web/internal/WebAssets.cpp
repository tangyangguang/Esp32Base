#include "../../Esp32BaseProfile.h"

#if ESP32BASE_ENABLE_WEB

#include "WebInternal.h"
#include "WebGzip.h"

namespace esp32base_web {

const char WEB_CSS_TYPE[] PROGMEM = "text/css; charset=utf-8";
#include "WebCssGzip.inc"
#include "WebPageAssetsGzip.inc"

const char WEB_HEAD[] PROGMEM =
    "<meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<link rel='stylesheet' href='/esp32base/ui.css?v=" ESP32BASE_WEB_CSS_VERSION "'>"
    "<script src='/esp32base/ui.js?v=" WEB_SCRIPT_ASSET_VERSION "'></script>";

#if ESP32BASE_ENABLE_FS || ESP32BASE_ENABLE_OTA
const char WEB_UPLOAD_HELPERS[] PROGMEM =
    "<script>function ebFmtBytes(n){var u=['B','KB','MB','GB'],i=0,x=n;while(x>=1024&&i<u.length-1){x/=1024;i++;}return (i?x.toFixed(2):Math.round(x))+' '+u[i];}</script>";
#endif

#if ESP32BASE_ENABLE_APP_CONFIG
// Page-scoped CSS for /esp32base/app-config. Kept out of WEB_HEAD so Status, Logs, WiFi,
// Auth, OTA and Tools pages do not fetch App Config-only styles.
const char WEB_APPCFG_STYLE[] PROGMEM =
    "<link rel='stylesheet' href='/esp32base/app-config.css?v=" WEB_APPCFG_ASSET_VERSION "'>";
#endif

#if ESP32BASE_ENABLE_APP_CONFIG
const char WEB_APPCFG_SCRIPT_TAG[] PROGMEM =
    "<script src='/esp32base/app-config.js?v=" WEB_APPCFG_SCRIPT_ASSET_VERSION "'></script>";
void handleAppConfigScript() {
    markRequest();
    const String encoding = g_server.header("Accept-Encoding");
    sendGzipAsset(g_server, encoding.c_str(), g_server.hasHeader("Accept-Encoding"),
                  "application/javascript; charset=utf-8", WEB_APPCFG_SCRIPT_GZIP, sizeof(WEB_APPCFG_SCRIPT_GZIP));
}
#endif

#if ESP32BASE_ENABLE_OTA
const char WEB_OTA_SCRIPT_TAG[] PROGMEM =
    "<script src='/esp32base/ota.js?v=" WEB_OTA_SCRIPT_ASSET_VERSION "'></script>";
void handleOtaScript() {
    markRequest();
    const String encoding = g_server.header("Accept-Encoding");
    sendGzipAsset(g_server, encoding.c_str(), g_server.hasHeader("Accept-Encoding"),
                  "application/javascript; charset=utf-8", WEB_OTA_SCRIPT_GZIP, sizeof(WEB_OTA_SCRIPT_GZIP));
}
#endif

#if ESP32BASE_ENABLE_FS
const char WEB_FS_SCRIPT_TAG[] PROGMEM =
    "<script src='/esp32base/fs.js?v=" WEB_FS_SCRIPT_ASSET_VERSION "'></script>";
void handleFsScript() {
    markRequest();
    const String encoding = g_server.header("Accept-Encoding");
    sendGzipAsset(g_server, encoding.c_str(), g_server.hasHeader("Accept-Encoding"),
                  "application/javascript; charset=utf-8", WEB_FS_SCRIPT_GZIP, sizeof(WEB_FS_SCRIPT_GZIP));
}
#endif

void handleUiScript() {
    markRequest();
    const String encoding = g_server.header("Accept-Encoding");
    sendGzipAsset(g_server, encoding.c_str(), g_server.hasHeader("Accept-Encoding"),
                  "application/javascript; charset=utf-8", WEB_SCRIPT_GZIP, sizeof(WEB_SCRIPT_GZIP));
}

#if ESP32BASE_ENABLE_APP_CONFIG
void handleAppConfigCss() {
    markRequest();
    const String encoding = g_server.header("Accept-Encoding");
    sendGzipAsset(g_server, encoding.c_str(), g_server.hasHeader("Accept-Encoding"),
                  WEB_CSS_TYPE, WEB_APPCFG_GZIP, sizeof(WEB_APPCFG_GZIP));
}
#endif

void handleUiCss() {
    markRequest();
    const String encoding = g_server.header("Accept-Encoding");
    sendGzipAsset(g_server, encoding.c_str(), g_server.hasHeader("Accept-Encoding"),
                  WEB_CSS_TYPE, WEB_CSS_GZIP, sizeof(WEB_CSS_GZIP));
}

} // namespace esp32base_web

#endif
