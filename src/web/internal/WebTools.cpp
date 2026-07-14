#include "../../Esp32BaseProfile.h"

#if ESP32BASE_ENABLE_WEB

#include "WebInternal.h"

namespace esp32base_web {

#if ESP32BASE_ENABLE_FILELOG
void sendFileLogModeOption(const char* value, const char* label, Esp32BaseFileLog::Mode mode) {
    sendChunk("<label><input type='radio' name='mode' value='");
    sendEscapedHtmlChunk(value);
    sendChunk("'");
    if (Esp32BaseFileLog::mode() == mode) {
        sendChunk(" checked");
    }
    sendChunk("> ");
    sendEscapedHtmlChunk(label);
    sendChunk("</label> ");
}

bool fileLogModeFromArg(const String& raw, Esp32BaseFileLog::Mode& mode) {
    if (raw == "off") {
        mode = Esp32BaseFileLog::OFF;
        return true;
    }
#if ESP32BASE_LOG_LEVEL >= ESP32BASE_LOG_ERROR
    if (raw == "error") {
        mode = Esp32BaseFileLog::ERROR;
        return true;
    }
#endif
#if ESP32BASE_LOG_LEVEL >= ESP32BASE_LOG_WARN
    if (raw == "warn") {
        mode = Esp32BaseFileLog::WARN;
        return true;
    }
#endif
#if ESP32BASE_LOG_LEVEL >= ESP32BASE_LOG_INFO
    if (raw == "info") {
        mode = Esp32BaseFileLog::INFO;
        return true;
    }
#endif
    return false;
}

const char* fileLogModeName(Esp32BaseFileLog::Mode mode) {
    switch (mode) {
        case Esp32BaseFileLog::ERROR: return "ERROR";
        case Esp32BaseFileLog::WARN: return "WARN";
        case Esp32BaseFileLog::INFO: return "INFO";
        case Esp32BaseFileLog::OFF:
        default: return "OFF";
    }
}

const char* fileLogRuntimeStateName() {
    if (Esp32BaseFileLog::faulted()) {
        return "write fault";
    }
    if (Esp32BaseFileLog::isEnabled()) {
        return "enabled";
    }
    return Esp32BaseFileLog::mode() == Esp32BaseFileLog::OFF ? "disabled" : "unavailable";
}

Esp32BaseWeb::UiTone fileLogRuntimeStateTone() {
    return Esp32BaseFileLog::faulted() ? Esp32BaseWeb::UI_WARN :
           (Esp32BaseFileLog::isEnabled() ? Esp32BaseWeb::UI_OK :
            (Esp32BaseFileLog::mode() == Esp32BaseFileLog::OFF ? Esp32BaseWeb::UI_INFO : Esp32BaseWeb::UI_WARN));
}

bool fileLogHasRuntimeDetails() {
    return Esp32BaseFileLog::faulted() || Esp32BaseFileLog::isEnabled() || Esp32BaseFileLog::mode() != Esp32BaseFileLog::OFF;
}

void sendFileLogRuntimeStateTag() {
    sendStatusTag(fileLogRuntimeStateTone(), fileLogRuntimeStateName());
}

void sendFileLogRuntimeStateRow(const char* label) {
    sendInfoRowStart(label);
    sendFileLogRuntimeStateTag();
    sendInfoRowEnd();
}

void sendFileLogRuntimeNotice() {
    if (Esp32BaseFileLog::faulted()) {
        sendChunk("<p class='notice warn'>New system log writes are stopped after a FS write failure. Existing system diagnostic logs may still be readable. Clear space or save the system log mode again after maintenance.</p>");
    } else if (!Esp32BaseFileLog::isEnabled() && Esp32BaseFileLog::mode() != Esp32BaseFileLog::OFF) {
        sendChunk("<p class='notice warn'>System logs are unavailable because FileLog could not initialize with the current filesystem state. Check LittleFS, format if needed, or save the system log mode again after maintenance.</p>");
    } else if (Esp32BaseFileLog::mode() == Esp32BaseFileLog::OFF) {
        sendChunk("<p class='notice info'>System log mode is OFF. Existing system diagnostic logs are historical; new logs are not written.</p>");
    }
}
#endif

void sendFooterBarModeOption(const char* value, const char* label, Esp32BaseWeb::FooterBarMode mode) {
    sendChunk("<label><input type='radio' name='mode' value='");
    sendEscapedHtmlChunk(value);
    sendChunk("'");
    if (Esp32BaseWeb::footerBarMode() == mode) {
        sendChunk(" checked");
    }
    sendChunk("> ");
    sendEscapedHtmlChunk(label);
    sendChunk("</label> ");
}

bool footerBarModeFromArg(const String& raw, Esp32BaseWeb::FooterBarMode& mode) {
    if (raw == "off") {
        mode = Esp32BaseWeb::FOOTER_BAR_OFF;
        return true;
    }
    if (raw == "status") {
        mode = Esp32BaseWeb::FOOTER_BAR_STATUS_ONLY;
        return true;
    }
    if (raw == "full") {
        mode = Esp32BaseWeb::FOOTER_BAR_FULL;
        return true;
    }
    return false;
}

#if ESP32BASE_ENABLE_WATCHDOG
void sendWatchdogPanel() {
    const WatchdogTripState state = readWatchdogTripState();
    char count[32];
    char resetAt[32];
    formatWatchdogTripResetAt(state, resetAt, sizeof(resetAt));
    snprintf(count, sizeof(count), "%lu",
             static_cast<unsigned long>(state.lifetime));
    sendChunk("<section class='panel actionpanel'><h2>Watchdog trip</h2><div class='tablewrap'><table class='kv'>");
    sendInfoRow("Lifetime resets", count);
    if (state.invalidBase) {
        sendInfoRow("Trip resets", "invalid baseline");
    } else {
        snprintf(count, sizeof(count), "%lu",
                 static_cast<unsigned long>(state.trip));
        sendInfoRow("Trip resets", count);
    }
    sendInfoRow("Trip reset at", resetAt);
    sendChunk("</table></div><form method='post' action='/esp32base/tools/watchdog-trip-reset' onsubmit=\"return confirm('Reset Watchdog trip counter? Lifetime resets are kept.')&&once(this)\"><div class='actions'><input type='submit' value='Reset Watchdog Trip'></div></form></section>");
}
#endif

void handleRestart() {
    markRequest();
    if (!ensurePostAllowed("restart")) {
        return;
    }
    g_server.send(200, "text/plain", "restarting");
    delay(100);
    Esp32BaseSystem::restart("web");
}

void handleToolsPage() {
    markRequest();
    if (!ensureAuth()) {
        return;
    }
    Esp32BaseWeb::sendHeader(g_builtinLabels[Esp32BaseWeb::BUILTIN_TOOLS]);
    Esp32BaseWeb::sendPageTitle(g_builtinLabels[Esp32BaseWeb::BUILTIN_TOOLS], "Low-frequency device settings and maintenance actions.");
    if (g_server.hasArg("formatted")) {
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_OK, "LittleFS formatted", "System stores were recreated and system log mode was reloaded.");
    } else if (g_server.hasArg("logs_cleared")) {
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_OK, "System logs cleared");
#if ESP32BASE_ENABLE_APP_EVENTS
    } else if (g_server.hasArg("app_events_cleared")) {
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_OK, "App Events cleared");
#endif
    } else if (g_server.hasArg("filelog_saved")) {
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_OK, "System log mode saved");
    } else if (g_server.hasArg("footer_saved")) {
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_OK, "Footer bar mode saved");
    } else if (g_server.hasArg("watchdog_trip_reset")) {
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_OK, "Watchdog trip reset");
    } else if (g_server.hasArg("error")) {
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_DANGER, "System action failed", g_server.arg("error").c_str());
    } else if (g_server.hasArg("restarting")) {
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_OK, "Restart requested", "Device is restarting. Please wait a few seconds, then reload.");
    }
    if (g_server.hasArg("hostname_saved")) {
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_OK, "Hostname saved", "Restart the device for mDNS, OTA and Web discovery to use it.");
    } else if (g_server.hasArg("hostname_error")) {
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_DANGER, "Hostname was not saved", g_server.arg("hostname_error").c_str());
    }
    sendChunk("<section class='panel actionpanel'><h2>System settings</h2><div class='toollinks'>");
#if ESP32BASE_ENABLE_APP_CONFIG
    Esp32BaseWeb::sendInfoRowCompactLink("App Config", "Application configuration values registered by the app.", nullptr, "/esp32base/app-config", "Open", Esp32BaseWeb::UI_INFO);
#endif
    Esp32BaseWeb::sendInfoRowCompactLink("WiFi Setup", "Stored credentials used by station mode and WiFi recovery.", nullptr, "/esp32base/wifi", "Open", Esp32BaseWeb::UI_INFO);
    Esp32BaseWeb::sendInfoRowCompactLink("Web Auth", "HTTP Basic Auth credentials for built-in routes.", nullptr, "/esp32base/auth", "Open", Esp32BaseWeb::UI_INFO);
#if ESP32BASE_ENABLE_OTA
    Esp32BaseWeb::sendInfoRowCompactLink("Firmware OTA", "Authenticated firmware upload endpoint.", nullptr, "/esp32base/ota", "Open", Esp32BaseWeb::UI_INFO);
#endif
#if ESP32BASE_ENABLE_FS
    Esp32BaseWeb::sendInfoRowCompactLink("File system", "Read-only LittleFS inventory and size summary.", nullptr, "/esp32base/fs", "Open", Esp32BaseWeb::UI_INFO);
#endif
    sendChunk("</div></section>");
    char storedHostname[64] = "";
    const bool hasStoredHostname = loadStoredHostname(storedHostname, sizeof(storedHostname));
    sendChunk("<div class='toolgrid'>");
    sendChunk("<section class='panel formpanel hostpanel'><h2>Hostname</h2><div class='hostfacts'><span><b>Current</b>");
    sendEscapedHtmlChunk(Esp32Base::hostname());
    sendChunk("</span><span><b>Default</b>");
    sendEscapedHtmlChunk(Esp32Base::defaultHostname());
    sendChunk("</span><span><b>Stored</b>");
    sendEscapedHtmlChunk(hasStoredHostname && storedHostname[0] ? storedHostname : "-");
    sendChunk("</span><span><b>Restart</b>");
    if (hostnameRestartRequired(storedHostname)) {
        sendChunk("<span class='tag warn'>required</span>");
    } else {
        sendChunk("no");
    }
    sendChunk("</span></div><form class='editform' method='post' action='/esp32base/tools/hostname' onsubmit=\"var h=this.hostname.value.trim();if(!/^[a-z0-9](?:[a-z0-9-]{0,30}[a-z0-9])?$/.test(h)){alert('Use 1-32 lowercase letters, digits and hyphen. No leading or trailing hyphen. Do not include .local.');return false;}this.hostname.value=h;return once(this);\"><div class='hostedit'>");
    sendChunk("<div class='field'><label for='host'>New hostname</label><input id='host' name='hostname' maxlength='32' autocomplete='off' value='");
    sendEscapedHtmlChunk(hasStoredHostname && storedHostname[0] ? storedHostname : Esp32Base::hostname());
    sendChunk("'><small>Saved hostname takes effect after restart.</small></div><div class='actions'><input type='submit' value='Save Hostname'></div></div></form></section>");
    sendChunk("<section class='panel actionpanel'><h2>Footer bar</h2><div class='tablewrap'><table class='kv'>");
    sendInfoRow("Current mode", Esp32BaseWeb::footerBarModeName());
    sendChunk("</table></div><form method='post' action='/esp32base/tools/footer-bar' onsubmit=\"return once(this)\"><div class='radioopts'>");
    sendFooterBarModeOption("off", "Off", Esp32BaseWeb::FOOTER_BAR_OFF);
    sendFooterBarModeOption("status", "Status only", Esp32BaseWeb::FOOTER_BAR_STATUS_ONLY);
    sendFooterBarModeOption("full", "Links + status", Esp32BaseWeb::FOOTER_BAR_FULL);
    sendChunk("</div><p class='muted'>Controls the compact bottom bar on built-in and app pages.</p><div class='actions'><input type='submit' value='Save Footer Bar'></div></form></section>");
#if ESP32BASE_ENABLE_FILELOG
    sendChunk("<section class='panel actionpanel'><h2>System logs</h2><div class='tablewrap'><table class='kv'>");
    sendInfoRow("Current mode", Esp32BaseFileLog::modeName());
    sendFileLogRuntimeStateRow("Runtime state");
    sendInfoRow("Path", Esp32BaseFileLog::path());
    sendChunk("</table></div>");
    sendFileLogRuntimeNotice();
    sendChunk("<form method='post' action='/esp32base/tools/filelog' onsubmit=\"return once(this)\"><div class='radioopts'>");
    sendFileLogModeOption("off", "Off", Esp32BaseFileLog::OFF);
#if ESP32BASE_LOG_LEVEL >= ESP32BASE_LOG_ERROR
    sendFileLogModeOption("error", "ERROR", Esp32BaseFileLog::ERROR);
#endif
#if ESP32BASE_LOG_LEVEL >= ESP32BASE_LOG_WARN
    sendFileLogModeOption("warn", "WARN", Esp32BaseFileLog::WARN);
#endif
#if ESP32BASE_LOG_LEVEL >= ESP32BASE_LOG_INFO
    sendFileLogModeOption("info", "INFO", Esp32BaseFileLog::INFO);
#endif
    sendChunk("</div><p class='muted'>System log mode is capped by the build log level.</p><div class='actions'><input type='submit' value='Save System Logs'></div></form></section>");
#else
    sendChunk("<section class='panel actionpanel'><h2>System logs</h2><p class='muted'>System diagnostic logs are unavailable in this profile.</p></section>");
#endif
#if ESP32BASE_ENABLE_WATCHDOG
    sendWatchdogPanel();
#endif
    sendChunk("</div><div class='toolgrid'>");
    sendChunk("<section class='panel dangerpanel'><h2>Restart device</h2><p class='muted'>Restart the device through the normal lifecycle path.</p><form method='post' action='/esp32base/tools/reboot' onsubmit=\"return confirm('Reboot device now?')&&once(this)\"><div class='actions'><input class='danger' type='submit' value='Restart device'></div></form></section>");
#if ESP32BASE_ENABLE_FILELOG
    sendChunk("<section class='panel dangerpanel'><h2>Clear system logs</h2><p class='dangertext'>Delete all system diagnostic log contents. Runtime settings and WiFi credentials are not changed.</p><form method='post' action='/esp32base/tools/logs-clear' onsubmit=\"return confirm('Clear system logs?')&&once(this)\"><div class='actions'><input class='danger' type='submit' value='Clear System Logs'></div></form></section>");
#else
    sendChunk("<section class='panel actionpanel'><h2>Clear system logs</h2><p class='muted'>System diagnostic logs are unavailable in this profile.</p></section>");
#endif
#if ESP32BASE_ENABLE_APP_EVENTS
    sendChunk("<section class='panel dangerpanel'><h2>Clear App Events</h2><p class='dangertext'>Delete the application event log store. System diagnostic logs, runtime settings and WiFi credentials are not changed.</p><form method='post' action='/esp32base/tools/app-events-clear' onsubmit=\"return confirm('Clear App Events?')&&once(this)\"><div class='actions'><input class='danger' type='submit' value='Clear App Events'></div></form></section>");
#endif
#if ESP32BASE_ENABLE_FS
    sendChunk("<section class='panel dangerpanel'><h2>Format LittleFS</h2><p class='dangertext'>This deletes logs and all files stored in LittleFS. WiFi, Web Auth and NVS config are not cleared.</p><form method='post' action='/esp32base/tools/format-fs' onsubmit=\"return confirm('Format LittleFS? This deletes logs and all files stored in LittleFS.')&&once(this)\"><div class='actions'><input class='danger' type='submit' value='Format LittleFS'></div></form></section>");
#else
    sendChunk("<section class='panel actionpanel'><h2>Format LittleFS</h2><p class='muted'>LittleFS is unavailable in this profile.</p></section>");
#endif
    sendChunk("</div>");
    Esp32BaseWeb::sendFooter();
}

void handleToolsFileLogPost() {
    markRequest();
    if (!ensurePostAllowed("tools_filelog")) {
        return;
    }
#if ESP32BASE_ENABLE_FILELOG
    Esp32BaseFileLog::Mode mode = Esp32BaseFileLog::OFF;
    if (!fileLogModeFromArg(g_server.arg("mode"), mode)) {
        ESP32BASE_LOG_W("web", "filelog_mode_rejected source=tools value=%s", g_server.arg("mode").c_str());
        redirectSeeOther("/esp32base/tools?error=filelog_invalid_mode");
        return;
    }
    ESP32BASE_LOG_W("web", "filelog_mode_requested source=tools from=%s to=%s",
                    Esp32BaseFileLog::modeName(),
                    fileLogModeName(mode));
    const bool ok = Esp32BaseFileLog::setMode(mode);
    redirectSeeOther(ok ? "/esp32base/tools?filelog_saved=1" : "/esp32base/tools?error=filelog_save_failed");
#else
    redirectSeeOther("/esp32base/tools?error=filelog_unavailable");
#endif
}

void handleToolsFooterBarPost() {
    markRequest();
    if (!ensurePostAllowed("tools_footer_bar")) {
        return;
    }
    Esp32BaseWeb::FooterBarMode mode = Esp32BaseWeb::FOOTER_BAR_FULL;
    if (!footerBarModeFromArg(g_server.arg("mode"), mode)) {
        ESP32BASE_LOG_W("web", "footer_bar_mode_rejected source=tools value=%s", g_server.arg("mode").c_str());
        redirectSeeOther("/esp32base/tools?error=footer_bar_invalid_mode");
        return;
    }
    ESP32BASE_LOG_W("web", "footer_bar_mode_requested source=tools from=%s to=%s",
                    Esp32BaseWeb::footerBarModeName(),
                    footerBarModeName(mode));
    const bool ok = Esp32BaseWeb::setFooterBarMode(mode);
    redirectSeeOther(ok ? "/esp32base/tools?footer_saved=1" : "/esp32base/tools?error=footer_bar_save_failed");
}

void handleToolsRebootPost() {
    markRequest();
    if (!ensurePostAllowed("tools_reboot")) {
        return;
    }
    ESP32BASE_LOG_W("web", "restart_requested source=tools");
    Esp32BaseWeb::sendHeader("Rebooting");
    sendChunk("<script>history.replaceState(null,'','/esp32base/tools?restarting=1');</script>");
    Esp32BaseWeb::sendPageTitle("Rebooting", "Device restart was accepted.");
    sendChunk("<section class='panel actionpanel'><h2>Restart requested</h2><p class='muted'>Device is restarting. Please wait a few seconds, then reload the System page.</p><div class='actions'><a class='btnlink' href='/esp32base/tools'>Reload System</a></div></section>");
    Esp32BaseWeb::sendFooter();
    delay(100);
    Esp32BaseSystem::restart("web");
}

#if ESP32BASE_ENABLE_WATCHDOG
void handleToolsWatchdogTripResetPost() {
    markRequest();
    if (!ensurePostAllowed("tools_watchdog_trip_reset")) {
        return;
    }
    const uint32_t lifetime = Esp32BaseWatchdog::lifetimeResetCount();
    const uint32_t resetTime = currentWatchdogTripResetTime();
    const bool writeOk = Esp32BaseConfig::setInt("eb_sys", "wdt_trip_base", static_cast<int32_t>(lifetime)) &&
                         Esp32BaseConfig::setInt("eb_sys", "wdt_trip_time", static_cast<int32_t>(resetTime));
    const bool verifyOk = Esp32BaseConfig::getInt("eb_sys", "wdt_trip_base", -1) == static_cast<int32_t>(lifetime) &&
                          Esp32BaseConfig::getInt("eb_sys", "wdt_trip_time", -1) == static_cast<int32_t>(resetTime);
    const bool ok = writeOk && verifyOk;
    ESP32BASE_LOG_W("web", "watchdog_trip_reset_requested source=tools lifetime=%lu time=%lu result=%s",
                    static_cast<unsigned long>(lifetime),
                    static_cast<unsigned long>(resetTime),
                    ok ? "success" : "failed");
    redirectSeeOther(ok ? "/esp32base/tools?watchdog_trip_reset=1" : "/esp32base/tools?error=watchdog_trip_reset_failed");
}
#endif

void handleToolsFormatFsPost() {
    markRequest();
    if (!ensurePostAllowed("tools_format_fs")) {
        return;
    }
#if ESP32BASE_ENABLE_FS
    ESP32BASE_LOG_W("web", "fs_format_requested source=tools");
#if ESP32BASE_ENABLE_FILELOG
    Esp32BaseFileLog::flush();
#endif
    const bool formatted = Esp32BaseFs::format();
    const bool mounted = formatted && Esp32BaseFs::begin();
    bool fileLogReloaded = false;
    bool appEventsRecreated = false;
#if ESP32BASE_ENABLE_FILELOG
    if (mounted) {
        fileLogReloaded = Esp32BaseFileLog::begin();
    }
#endif
#if ESP32BASE_ENABLE_APP_EVENTS
    if (mounted) {
        appEventsRecreated = Esp32BaseAppEvents::reload();
    }
#endif
    ESP32BASE_LOG_W("web", "fs_format_completed source=tools format=%s mount=%s filelog_reload=%s app_events_recreate=%s",
                    formatted ? "success" : "failed",
                    mounted ? "success" : "failed",
                    fileLogReloaded ? "success" : "skipped",
                    appEventsRecreated ? "success" : "skipped");
    if (formatted) {
        notifyToolsFormatFsSuccess(mounted, fileLogReloaded);
    }
    if (formatted && mounted
#if ESP32BASE_ENABLE_APP_EVENTS
        && appEventsRecreated
#endif
    ) {
        redirectSeeOther("/esp32base/tools?formatted=1");
    } else {
        redirectSeeOther(!formatted ? "/esp32base/tools?error=format_failed" :
                         !mounted ? "/esp32base/tools?error=mount_failed" :
                         "/esp32base/tools?error=app_events_recreate_failed");
    }
#else
    ESP32BASE_LOG_W("web", "fs_format_requested source=tools result=unavailable");
    redirectSeeOther("/esp32base/tools?error=fs_unavailable");
#endif
}

void handleToolsLogsClearPost() {
    markRequest();
    if (!ensurePostAllowed("tools_logs_clear")) {
        return;
    }
#if ESP32BASE_ENABLE_FILELOG
    const bool ok = Esp32BaseFileLog::clear();
    redirectSeeOther(ok ? "/esp32base/tools?logs_cleared=1" : "/esp32base/tools?error=logs_clear_failed");
#else
    redirectSeeOther("/esp32base/tools?error=logs_unavailable");
#endif
}

#if ESP32BASE_ENABLE_APP_EVENTS
void handleToolsAppEventsClearPost() {
    markRequest();
    if (!ensurePostAllowed("tools_app_events_clear")) {
        return;
    }
    const bool ok = Esp32BaseAppEvents::clear();
    redirectSeeOther(ok ? "/esp32base/tools?app_events_cleared=1" : "/esp32base/tools?error=app_events_clear_failed");
}
#endif

} // namespace esp32base_web

#endif
