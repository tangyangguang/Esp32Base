#include "../../Esp32BaseProfile.h"

#if ESP32BASE_ENABLE_WEB

#include "WebInternal.h"

namespace esp32base_web {

void handleWifiPage() {
    markRequest();
    if (!ensureAuth()) {
        return;
    }
    char ssid[33] = "";
    char password[65] = "";
    Esp32BaseConfig::getStr("eb_wifi", "ssid", ssid, sizeof(ssid), Esp32BaseWiFi::ssid());
    Esp32BaseConfig::getStr("eb_wifi", "pass", password, sizeof(password), "");

    Esp32BaseWeb::sendHeader(g_builtinLabels[Esp32BaseWeb::BUILTIN_WIFI]);
    Esp32BaseWeb::sendPageTitle("WiFi Settings", "Stored credentials used by station mode and WiFi recovery.");
    if (g_server.hasArg("saved")) {
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_OK, "Saved", "Credentials updated and connection started.");
    } else if (g_server.hasArg("error")) {
        if (g_server.arg("error") == "clear_failed") {
            Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_DANGER, "WiFi credentials were not cleared");
        } else {
            Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_DANGER, "WiFi settings were not saved");
        }
    } else if (g_server.hasArg("cleared")) {
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_OK, "Saved WiFi credentials were cleared");
    }
    sendChunk("<div class='toolgrid'><section class='panel formpanel wifipanel'><h2>Credentials</h2><form class='editform' method='post' action='/esp32base/wifi' onsubmit=\"if(!this.ssid.value.trim()){alert('SSID cannot be empty');return false;}if(this.ssid.value.length>32||this.password.value.length>64){alert('SSID or password is too long');return false;}return once(this);\"><div class='fieldgrid'>");
    sendChunk("<div class='field long'><label for='ssid'>SSID</label><input id='ssid' name='ssid' value='");
    sendEscapedHtmlChunk(ssid);
    sendChunk("' maxlength='32' autocomplete='off'><small>1-32 characters.</small></div><div class='field long'><label for='wp'>Password (optional)</label><input id='wp' type='password' name='password' value='");
    sendEscapedHtmlChunk(password);
    sendChunk("' maxlength='64'><small>Leave empty for open networks.</small></div></div><div class='actions'><input class='secondary' type='button' value='Show/Hide Password' onclick=\"var p=document.getElementById('wp');p.type=p.type=='password'?'text':'password'\"><input type='submit' value='Save &amp; Connect'></div></form></section>");
    sendChunk("<section class='panel dangerpanel'><h2>Clear WiFi</h2><p class='muted'>Remove stored WiFi credentials from this device.</p><form method='post' action='/esp32base/api/wifi/clear' onsubmit=\"return confirm('Clear WiFi credentials?')&&once(this)\"><div class='actions'><input class='danger' type='submit' value='Clear WiFi'></div></form></section></div>");
    Esp32BaseWeb::sendFooter();
}

void handleWifiSubmit() {
    markRequest();
    if (!ensurePostAllowed("wifi_submit")) {
        return;
    }
    const String ssid = g_server.arg("ssid");
    const String password = g_server.arg("password");
    if (ssid.length() == 0 || ssid.length() > 32 || password.length() > 64) {
        redirectSeeOther("/esp32base/wifi?error=invalid");
        return;
    }
    const bool ok = Esp32BaseConfig::setStr("eb_wifi", "ssid", ssid.c_str()) &&
                    Esp32BaseConfig::setStr("eb_wifi", "pass", password.c_str());
    ESP32BASE_LOG_I("web", "wifi form submitted ssid=%s password=%s result=%s",
                    ssid.c_str(), password.c_str(), ok ? "success" : "failed");
    redirectSeeOther(ok ? "/esp32base/wifi?saved=1" : "/esp32base/wifi?error=save_failed");
    if (ok) {
        delay(250);
        Esp32BaseWiFi::connect(ssid.c_str(), password.c_str(), false);
    }
}

void handleWifiClear() {
    markRequest();
    if (!ensurePostAllowed("wifi_clear")) {
        return;
    }
    const bool ok = Esp32BaseWiFi::clearCredentials();
    redirectSeeOther(ok ? "/esp32base/wifi?cleared=1" : "/esp32base/wifi?error=clear_failed");
}

} // namespace esp32base_web

#endif
