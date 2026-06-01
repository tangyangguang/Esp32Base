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
    const bool hasSavedPassword = password[0] != '\0';

    Esp32BaseWeb::sendHeader(g_builtinLabels[Esp32BaseWeb::BUILTIN_WIFI]);
    Esp32BaseWeb::sendPageTitle("WiFi Settings", "Stored credentials used by station mode and WiFi recovery.");
    if (g_server.hasArg("saved")) {
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_OK, "Saved", "Credentials updated and connection started.");
    } else if (g_server.hasArg("retry")) {
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_OK, "Retry started", "Saved WiFi credentials are being retried.");
    } else if (g_server.hasArg("error")) {
        if (g_server.arg("error") == "clear_failed") {
            Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_DANGER, "WiFi credentials were not cleared");
        } else if (g_server.arg("error") == "retry_failed") {
            Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_DANGER, "Saved WiFi credentials could not be retried");
        } else {
            Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_DANGER, "WiFi settings were not saved");
        }
    } else if (g_server.hasArg("cleared")) {
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_OK, "Saved WiFi credentials were cleared");
    }
    sendChunk("<div class='toolgrid'><section class='panel formpanel wifipanel'><h2>Credentials</h2><form class='editform' method='post' action='/esp32base/wifi' onsubmit=\"if(!this.ssid.value.trim()){alert('SSID cannot be empty');return false;}if(this.ssid.value.length>32||this.password.value.length>64){alert('SSID or password is too long');return false;}return once(this);\"><div class='fieldgrid'>");
    sendChunk("<div class='field long'><label for='ssid'>SSID</label><input id='ssid' name='ssid' value='");
    sendEscapedHtmlChunk(ssid);
    sendChunk("' maxlength='32' autocomplete='off'><small>1-32 characters.</small></div><div class='field long'><label for='wp'>New password</label><input id='wp' type='password' name='password' value='' maxlength='64' autocomplete='new-password'><small>");
    sendChunk(hasSavedPassword ? "Leave empty to keep the saved password." : "Leave empty for an open network.");
    sendChunk("</small></div><div class='field long'><label><input type='checkbox' name='clear_password' value='1'> Clear saved password for open network</label><small>Saved password: ");
    sendChunk(hasSavedPassword ? "set" : "not set");
    sendChunk("</small></div></div><div class='actions'><input class='secondary' type='button' value='Show/Hide Password' onclick=\"var p=document.getElementById('wp');p.type=p.type=='password'?'text':'password'\"><input type='submit' value='Save &amp; Connect'></div></form></section>");
    if (ssid[0] && Esp32BaseWiFi::safeBootPaused()) {
        sendChunk("<section class='panel actionpanel'><h2>WiFi recovery paused</h2><p class='muted'>Saved credentials are paused after repeated guarded STA resets.</p><p class='muted'>Guarded resets: ");
        sendIntChunk(Esp32BaseWiFi::safeBootGuardedResetCount());
        sendChunk("</p><form method='post' action='/esp32base/api/wifi/retry' onsubmit=\"return once(this)\"><div class='actions'><input type='submit' value='Retry Saved WiFi'></div></form></section>");
    }
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
    const bool clearPassword = g_server.hasArg("clear_password");
    if (ssid.length() == 0 || ssid.length() > 32 || password.length() > 64) {
        redirectSeeOther("/esp32base/wifi?error=invalid");
        return;
    }
    char existingPassword[65] = "";
    Esp32BaseConfig::getStr("eb_wifi", "pass", existingPassword, sizeof(existingPassword), "");
    const char* effectivePassword = existingPassword;
    const char* passwordState = "unchanged";
    if (clearPassword) {
        effectivePassword = "";
        passwordState = "cleared";
    } else if (password.length() > 0) {
        effectivePassword = password.c_str();
        passwordState = "updated";
    }
    const bool ok = Esp32BaseConfig::setStr("eb_wifi", "ssid", ssid.c_str()) &&
                    Esp32BaseConfig::setStr("eb_wifi", "pass", effectivePassword);
    ESP32BASE_LOG_I("web", "wifi form submitted ssid=%s password_state=%s result=%s",
                    ssid.c_str(), passwordState, ok ? "success" : "failed");
    redirectSeeOther(ok ? "/esp32base/wifi?saved=1" : "/esp32base/wifi?error=save_failed");
    if (ok) {
        delay(250);
        Esp32BaseWiFi::connect(ssid.c_str(), effectivePassword, false);
    }
}

void handleWifiRetry() {
    markRequest();
    if (!ensurePostAllowed("wifi_retry")) {
        return;
    }
    const bool ok = Esp32BaseWiFi::retrySavedCredentials();
    redirectSeeOther(ok ? "/esp32base/wifi?retry=1" : "/esp32base/wifi?error=retry_failed");
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
