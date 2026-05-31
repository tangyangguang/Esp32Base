#include "../../Esp32BaseProfile.h"

#if ESP32BASE_ENABLE_WEB

#include "WebInternal.h"

namespace esp32base_web {

void handleAuthPage() {
    markRequest();
    if (!ensureAuth()) {
        return;
    }
    Esp32BaseWeb::sendHeader(g_builtinLabels[Esp32BaseWeb::BUILTIN_AUTH]);
    Esp32BaseWeb::sendPageTitle(g_builtinLabels[Esp32BaseWeb::BUILTIN_AUTH], "Update the HTTP Basic Auth credentials used by built-in routes.");
    if (g_server.hasArg("saved")) {
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_OK, "Auth updated", "Use the new credentials on the next auth prompt.");
    } else if (g_server.hasArg("error")) {
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_DANGER, "Auth update failed", g_server.arg("error").c_str());
    }
    sendChunk("<section class='panel formpanel authpanel'><h2>Credentials</h2><form class='editform' method='post' action='/esp32base/auth' onsubmit=\"var f=this;function v(n){return f.elements[n].value.trim();}if(!v('current_user')||!v('current_pass')||!v('new_user')||!v('new_pass')||!v('confirm_pass')){alert('All auth fields are required');return false;}if(v('new_pass')!=v('confirm_pass')){alert('New passwords do not match');return false;}return once(this)\"><div class='fieldgrid'>");
    sendChunk("<div class='field med'><label for='current_user'>Current auth user</label><input id='current_user' name='current_user' maxlength='31' required value='");
    sendEscapedHtmlChunk(g_authUser);
    sendChunk("'></div>");
    sendChunk("<div class='field med'><label for='current_pass'>Current auth password</label><input id='current_pass' type='password' name='current_pass' maxlength='63' autocomplete='current-password' required></div>");
    sendChunk("<div class='field med'><label for='new_user'>New auth user</label><input id='new_user' name='new_user' maxlength='31' required value='");
    sendEscapedHtmlChunk(g_authUser);
    sendChunk("'></div>");
    sendChunk("<div class='field med'><label for='new_pass'>New auth password</label><input id='new_pass' type='password' name='new_pass' maxlength='63' autocomplete='new-password' required></div>");
    sendChunk("<div class='field med'><label for='confirm_pass'>Confirm new auth password</label><input id='confirm_pass' type='password' name='confirm_pass' maxlength='63' autocomplete='new-password' required></div></div>");
    sendChunk("<div class='actions'><input type='submit' value='Save Auth'></div></form></section>");
    Esp32BaseWeb::sendFooter();
}

void handleAuthSubmit() {
    markRequest();
    if (!ensurePostAllowed("auth_submit")) {
        return;
    }
    char currentUser[32];
    char currentPass[64];
    char newUser[32];
    char newPass[64];
    char confirmPass[64];
    strlcpy(currentUser, g_server.arg("current_user").c_str(), sizeof(currentUser));
    strlcpy(currentPass, g_server.arg("current_pass").c_str(), sizeof(currentPass));
    strlcpy(newUser, g_server.arg("new_user").c_str(), sizeof(newUser));
    strlcpy(newPass, g_server.arg("new_pass").c_str(), sizeof(newPass));
    strlcpy(confirmPass, g_server.arg("confirm_pass").c_str(), sizeof(confirmPass));
    if (!Esp32BaseWeb::verifyAuth(currentUser, currentPass)) {
        ESP32BASE_LOG_W("web", "auth_update_failed reason=current_auth");
        redirectSeeOther("/esp32base/auth?error=current_auth");
        return;
    }
    if (strcmp(newPass, confirmPass) != 0) {
        ESP32BASE_LOG_W("web", "auth_update_failed reason=confirm");
        redirectSeeOther("/esp32base/auth?error=confirm");
        return;
    }
    if (!Esp32BaseWeb::saveAuth(newUser, newPass)) {
        redirectSeeOther("/esp32base/auth?error=invalid_or_storage");
        return;
    }
    redirectSeeOther("/esp32base/auth?saved=1");
}

} // namespace esp32base_web

#endif
