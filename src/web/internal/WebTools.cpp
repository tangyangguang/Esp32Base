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

#if ESP32BASE_ENABLE_WIFI_RECOVERY_BUTTON
bool parseToolsUnsigned(const String& raw, uint32_t minimum, uint32_t maximum, uint32_t& out) {
    if (raw.length() == 0 || raw[0] == '-' || raw[0] == '+') {
        return false;
    }
    char* end = nullptr;
    const unsigned long value = strtoul(raw.c_str(), &end, 10);
    if (!end || *end != '\0' || value < minimum || value > maximum) {
        return false;
    }
    out = static_cast<uint32_t>(value);
    return true;
}

void sendWifiRecoveryPanel() {
    const Esp32BaseWiFi::RecoveryButtonConfig config = Esp32BaseWiFi::recoveryButtonConfig();
    const Esp32BaseWiFi::RecoveryButtonConfig defaults = Esp32BaseWiFi::defaultRecoveryButtonConfig();
    char value[32];
    sendChunk("<section class='panel actionpanel recoverypanel'><div class='paneltitle'><h2>WiFi recovery button</h2>");
    sendStatusTag(config.enabled ? Esp32BaseWeb::UI_OK : Esp32BaseWeb::UI_NEUTRAL,
                  config.enabled ? "enabled" : "disabled");
    sendChunk("</div><p class='muted recoveryintro'>Hold the configured button after startup to open the WiFi configuration hotspot.</p><div class='recoveryfacts'><span><b>GPIO</b><em>");
    snprintf(value, sizeof(value), "%d", static_cast<int>(config.gpio));
    sendEscapedHtmlChunk(value);
    sendChunk("</em></span><span><b>Long press</b><em>");
    snprintf(value, sizeof(value), "%lu seconds", static_cast<unsigned long>(config.holdMs / 1000UL));
    sendEscapedHtmlChunk(value);
    sendChunk("</em></span><span><b>Build default</b><em>");
    snprintf(value, sizeof(value), "GPIO %d · %lu seconds",
             static_cast<int>(defaults.gpio),
             static_cast<unsigned long>(defaults.holdMs / 1000UL));
    sendEscapedHtmlChunk(value);
    sendChunk("</em></span></div><form class='editform recoveryform' method='post' action='/esp32base/system/wifi-recovery' onsubmit=\"return once(this)\"><label class='recoverytoggle'><input type='checkbox' name='enabled' value='1'");
    if (config.enabled) {
        sendChunk(" checked");
    }
    sendChunk("><span><b>Enable physical recovery</b><small>Uses an active-low input with the internal pull-up.</small></span></label><div class='recoveryfields'><div class='field'><label for='wifi-recovery-gpio'>GPIO</label><input id='wifi-recovery-gpio' name='gpio' type='number' min='0' max='127' value='");
    sendIntChunk(config.gpio);
    sendChunk("' required></div><div class='field'><label for='wifi-recovery-hold'>Long press</label><div class='inputunit'><input id='wifi-recovery-hold' name='hold_seconds' type='number' min='1' max='60' step='1' value='");
    sendIntChunk(static_cast<int>(config.holdMs / 1000UL));
    sendChunk("' required><span>seconds</span></div></div></div>");
    sendChunk("<p class='notice warn recoverywarning'><b>Pin safety:</b> GPIO changes immediately. The library rejects invalid and known flash pins; board-level PSRAM, USB, peripheral and application conflicts remain the application's responsibility.</p>");
    sendChunk("<p class='muted recoverynote'>A successful hold preserves saved credentials and opens the hotspot at 192.168.4.1. Press the BOOT button only after startup; holding it during reset or power-on enters ROM download mode.</p>");
    sendChunk("<div class='actions'><input type='submit' value='Save Recovery Button'></div></form></section>");
}
#endif

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
    sendChunk("</table></div><form method='post' action='/esp32base/system/watchdog-trip-reset' onsubmit=\"return confirm('Reset Watchdog trip counter? Lifetime resets are kept.')&&once(this)\"><div class='actions'><input type='submit' value='Reset Watchdog Trip'></div></form></section>");
}
#endif

#if ESP32BASE_ENABLE_RECORD_STORE
Esp32BaseWeb::UiTone businessRecordStoreTone(Esp32BaseRecordStore::StoreState state) {
    return state == Esp32BaseRecordStore::StoreState::Ready ? Esp32BaseWeb::UI_OK :
           (state == Esp32BaseRecordStore::StoreState::Degraded ||
            state == Esp32BaseRecordStore::StoreState::WriteFault ? Esp32BaseWeb::UI_WARN :
            Esp32BaseWeb::UI_DANGER);
}

const char* businessRecordStoreDisplayName(const char* path) {
    if (!path || !path[0]) return "unknown";
    const char* slash = strrchr(path, '/');
    return slash && slash[1] ? slash + 1 : path;
}

void sendBusinessRecordStoresClearPanel() {
    const uint8_t count = businessRecordStoreCount();
    if (count == 0) return;
    bool allReady = true;
    sendChunk("<section class='panel dangerpanel'><h2>Clear Business Records</h2><p class='dangertext'>Logically clear every registered current-version business RecordStore. IDs continue increasing. App Events, system logs, settings, WiFi and historical unregistered versions are not changed.</p><div class='tablewrap'><table class='kv'>");
    for (uint8_t i = 0; i < count; ++i) {
        Esp32BaseRecordStore* store = businessRecordStoreAt(i);
        Esp32BaseRecordStore::StoreStatus status;
        const bool initialized = store && store->readStatus(status);
        const bool ready = initialized && status.ready;
        allReady = allReady && ready;
        sendInfoRowStart(initialized ? businessRecordStoreDisplayName(status.path) : "unknown");
        if (initialized) {
            sendStatusTag(businessRecordStoreTone(status.state), Esp32BaseRecordStore::storeStateName(status.state));
            sendChunk(" &middot; ");
            sendUintChunk(status.recordCount);
            sendChunk(" records");
            if (status.error != Esp32BaseRecordStore::StoreError::None) {
                sendChunk(" &middot; error ");
                sendEscapedHtmlChunk(status.errorReason ? status.errorReason : "unknown");
            }
        } else {
            sendStatusTag(Esp32BaseWeb::UI_DANGER, "not initialized");
        }
        sendInfoRowEnd();
    }
    sendChunk("</table></div>");
    if (allReady) {
        sendChunk("<form method='post' action='/esp32base/system/business-records-clear' onsubmit=\"return confirm('Clear all registered current-version business records? IDs will continue increasing.')&&once(this)\"><div class='actions'><input class='danger' type='submit' value='Clear Business Records'></div></form>");
    } else {
        sendChunk("<p class='notice warn'>No records will be cleared while any registered Store is unavailable or structurally invalid.</p><div class='actions'><input class='danger' type='submit' value='Clear Business Records' disabled></div>");
    }
    sendChunk("</section>");
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
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_OK, "LittleFS formatted", "System stores and registered business RecordStores were recreated, and system log mode was reloaded.");
    } else if (g_server.hasArg("logs_cleared")) {
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_OK, "System logs cleared");
#if ESP32BASE_ENABLE_RECORD_STORE
    } else if (g_server.hasArg("business_records_cleared")) {
        char message[96];
        snprintf(message, sizeof(message), "%lu current-version business Store(s) were logically cleared. IDs were preserved.",
                 static_cast<unsigned long>(strtoul(g_server.arg("business_records_cleared").c_str(), nullptr, 10)));
        Esp32BaseWeb::sendNotice(g_server.hasArg("cleanup_warning") ? Esp32BaseWeb::UI_WARN : Esp32BaseWeb::UI_OK,
                                 g_server.hasArg("cleanup_warning") ? "Business records cleared with cleanup warnings" : "Business records cleared",
                                 message);
    } else if (g_server.hasArg("business_records_clear_failed")) {
        char message[112];
        snprintf(message, sizeof(message), "%lu of %lu Store(s) were cleared before a failure. Check Store status and system logs before retrying.",
                 static_cast<unsigned long>(strtoul(g_server.arg("cleared").c_str(), nullptr, 10)),
                 static_cast<unsigned long>(strtoul(g_server.arg("total").c_str(), nullptr, 10)));
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_DANGER, "Business records were only partially cleared", message);
    } else if (g_server.hasArg("business_records_not_ready")) {
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_DANGER, "Business records were not cleared", "At least one registered Store was unavailable or structurally invalid. No Store was changed.");
#endif
#if ESP32BASE_ENABLE_APP_EVENTS
    } else if (g_server.hasArg("app_events_cleared")) {
#if ESP32BASE_ENABLE_APP_EVENT_CONDITIONS
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_OK, "App Event history cleared", "Confirmed condition states were preserved.");
#else
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_OK, "App Event history cleared");
#endif
#endif
    } else if (g_server.hasArg("filelog_saved")) {
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_OK, "System log mode saved");
    } else if (g_server.hasArg("footer_saved")) {
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_OK, "Footer bar mode saved");
#if ESP32BASE_ENABLE_WIFI_RECOVERY_BUTTON
    } else if (g_server.hasArg("wifi_recovery_saved")) {
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_OK, "WiFi recovery button saved", "The new GPIO and long-press behavior are active now.");
#endif
    } else if (g_server.hasArg("watchdog_trip_reset")) {
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_OK, "Watchdog trip reset");
#if ESP32BASE_ENABLE_APP_CONFIG
    } else if (g_server.hasArg("app_config_defaults_restored")) {
        char message[144];
        snprintf(message, sizeof(message),
                 "%lu stored field(s) were cleared; %lu effective value(s) changed.%s",
                 static_cast<unsigned long>(strtoul(g_server.arg("cleared").c_str(), nullptr, 10)),
                 static_cast<unsigned long>(strtoul(g_server.arg("changed").c_str(), nullptr, 10)),
                 g_server.hasArg("restart") ? " Restart the device for marked values to take effect." : "");
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_OK, "App Config defaults restored", message);
    } else if (g_server.hasArg("app_config_defaults_partial")) {
        char message[144];
        snprintf(message, sizeof(message),
                 "%lu stored field(s) were cleared and %lu effective value(s) changed before a failure. Retry after checking NVS and system logs.",
                 static_cast<unsigned long>(strtoul(g_server.arg("cleared").c_str(), nullptr, 10)),
                 static_cast<unsigned long>(strtoul(g_server.arg("changed").c_str(), nullptr, 10)));
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_DANGER, "App Config defaults were only partially restored", message);
    } else if (g_server.hasArg("app_config_defaults_rejected")) {
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_DANGER, "App Config defaults were not restored",
                                 "The app rejected its registered defaults during validation. No values were changed.");
#endif
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
    sendChunk("</span></div><form class='editform' method='post' action='/esp32base/system/hostname' onsubmit=\"var h=this.hostname.value.trim();if(!/^[a-z0-9](?:[a-z0-9-]{0,30}[a-z0-9])?$/.test(h)){alert('Use 1-32 lowercase letters, digits and hyphen. No leading or trailing hyphen. Do not include .local.');return false;}this.hostname.value=h;return once(this);\"><div class='hostedit'>");
    sendChunk("<div class='field'><label for='host'>New hostname</label><input id='host' name='hostname' maxlength='32' autocomplete='off' value='");
    sendEscapedHtmlChunk(hasStoredHostname && storedHostname[0] ? storedHostname : Esp32Base::hostname());
    sendChunk("'><small>Saved hostname takes effect after restart.</small></div><div class='actions'><input type='submit' value='Save Hostname'></div></div></form></section>");
#if ESP32BASE_ENABLE_WIFI_RECOVERY_BUTTON
    sendWifiRecoveryPanel();
#endif
    sendChunk("<section class='panel actionpanel'><h2>Footer bar</h2><div class='tablewrap'><table class='kv'>");
    sendInfoRow("Current mode", Esp32BaseWeb::footerBarModeName());
    sendChunk("</table></div><form method='post' action='/esp32base/system/footer-bar' onsubmit=\"return once(this)\"><div class='radioopts'>");
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
    sendChunk("<form method='post' action='/esp32base/system/filelog' onsubmit=\"return once(this)\"><div class='radioopts'>");
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
#if ESP32BASE_ENABLE_APP_CONFIG
    if (g_appConfigFieldCount > 0) {
        sendChunk("<section class='panel dangerpanel'><h2>Restore App Config Defaults</h2><p class='dangertext'>Remove every stored App Config value registered by the app and return to the defaults declared by this firmware. WiFi, Web Auth, Esp32Base settings, files, logs and unregistered configuration are not changed.</p><form method='post' action='/esp32base/system/app-config-defaults' onsubmit=\"return confirm('Restore all registered App Config values to firmware defaults? This cannot be undone.')&&once(this)\"><div class='actions'><input class='danger' type='submit' value='Restore App Config Defaults'></div></form></section>");
    }
#endif
    sendChunk("<section class='panel dangerpanel'><h2>Restart device</h2><p class='muted'>Restart the device through the normal lifecycle path.</p><form method='post' action='/esp32base/system/reboot' onsubmit=\"return confirm('Reboot device now?')&&once(this)\"><div class='actions'><input class='danger' type='submit' value='Restart device'></div></form></section>");
#if ESP32BASE_ENABLE_FILELOG
    sendChunk("<section class='panel dangerpanel'><h2>Clear system logs</h2><p class='dangertext'>Delete all system diagnostic log contents. Runtime settings and WiFi credentials are not changed.</p><form method='post' action='/esp32base/system/logs-clear' onsubmit=\"return confirm('Clear system logs?')&&once(this)\"><div class='actions'><input class='danger' type='submit' value='Clear System Logs'></div></form></section>");
#else
    sendChunk("<section class='panel actionpanel'><h2>Clear system logs</h2><p class='muted'>System diagnostic logs are unavailable in this profile.</p></section>");
#endif
#if ESP32BASE_ENABLE_RECORD_STORE
    sendBusinessRecordStoresClearPanel();
#endif
#if ESP32BASE_ENABLE_APP_EVENTS
#if ESP32BASE_ENABLE_APP_EVENT_CONDITIONS
    sendChunk("<section class='panel dangerpanel'><h2>Clear App Event History</h2><p class='dangertext'>Delete retained App Event records. Confirmed condition states, system diagnostic logs, runtime settings and WiFi credentials are not changed.</p><form method='post' action='/esp32base/system/app-events-clear' onsubmit=\"return confirm('Clear App Event history?')&&once(this)\"><div class='actions'><input class='danger' type='submit' value='Clear App Event History'></div></form></section>");
#else
    sendChunk("<section class='panel dangerpanel'><h2>Clear App Event History</h2><p class='dangertext'>Delete retained App Event records. System diagnostic logs, runtime settings and WiFi credentials are not changed.</p><form method='post' action='/esp32base/system/app-events-clear' onsubmit=\"return confirm('Clear App Event history?')&&once(this)\"><div class='actions'><input class='danger' type='submit' value='Clear App Event History'></div></form></section>");
#endif
#endif
#if ESP32BASE_ENABLE_FS
    sendChunk("<section class='panel dangerpanel'><h2>Format LittleFS</h2><p class='dangertext'>This deletes logs and all files stored in LittleFS. WiFi, Web Auth and NVS config are not cleared.</p><form method='post' action='/esp32base/system/format-fs' onsubmit=\"return confirm('Format LittleFS? This deletes logs and all files stored in LittleFS.')&&once(this)\"><div class='actions'><input class='danger' type='submit' value='Format LittleFS'></div></form></section>");
#else
    sendChunk("<section class='panel actionpanel'><h2>Format LittleFS</h2><p class='muted'>LittleFS is unavailable in this profile.</p></section>");
#endif
    sendChunk("</div>");
    Esp32BaseWeb::sendFooter();
}

#if ESP32BASE_ENABLE_WIFI_RECOVERY_BUTTON
void handleToolsWifiRecoveryPost() {
    markRequest();
    if (!ensurePostAllowed("tools_wifi_recovery")) {
        return;
    }
    uint32_t gpio = 0;
    uint32_t holdSeconds = 0;
    if (!parseToolsUnsigned(g_server.arg("gpio"), 0, INT8_MAX, gpio) ||
        !parseToolsUnsigned(g_server.arg("hold_seconds"), 1, 60, holdSeconds)) {
        ESP32BASE_LOG_W("web", "wifi_recovery_config_rejected source=tools reason=invalid_number");
        redirectSeeOther("/esp32base/system?error=wifi_recovery_invalid_value");
        return;
    }
    Esp32BaseWiFi::RecoveryButtonConfig config = {
        g_server.hasArg("enabled") && g_server.arg("enabled") == "1",
        static_cast<int8_t>(gpio),
        holdSeconds * 1000UL
    };
    if (!Esp32BaseWiFi::isValidRecoveryButtonConfig(config)) {
        ESP32BASE_LOG_W("web", "wifi_recovery_config_rejected source=tools reason=invalid_gpio enabled=%s gpio=%lu hold_seconds=%lu",
                        config.enabled ? "yes" : "no",
                        static_cast<unsigned long>(gpio),
                        static_cast<unsigned long>(holdSeconds));
        redirectSeeOther("/esp32base/system?error=wifi_recovery_invalid_gpio");
        return;
    }
    const bool ok = Esp32BaseWiFi::setRecoveryButtonConfig(config);
    redirectSeeOther(ok ? "/esp32base/system?wifi_recovery_saved=1" :
                          "/esp32base/system?error=wifi_recovery_save_failed");
}
#endif

void handleToolsFileLogPost() {
    markRequest();
    if (!ensurePostAllowed("tools_filelog")) {
        return;
    }
#if ESP32BASE_ENABLE_FILELOG
    Esp32BaseFileLog::Mode mode = Esp32BaseFileLog::OFF;
    if (!fileLogModeFromArg(g_server.arg("mode"), mode)) {
        ESP32BASE_LOG_W("web", "filelog_mode_rejected source=tools value=%s", g_server.arg("mode").c_str());
        redirectSeeOther("/esp32base/system?error=filelog_invalid_mode");
        return;
    }
    ESP32BASE_LOG_W("web", "filelog_mode_requested source=tools from=%s to=%s",
                    Esp32BaseFileLog::modeName(),
                    fileLogModeName(mode));
    const bool ok = Esp32BaseFileLog::setMode(mode);
    redirectSeeOther(ok ? "/esp32base/system?filelog_saved=1" : "/esp32base/system?error=filelog_save_failed");
#else
    redirectSeeOther("/esp32base/system?error=filelog_unavailable");
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
        redirectSeeOther("/esp32base/system?error=footer_bar_invalid_mode");
        return;
    }
    ESP32BASE_LOG_W("web", "footer_bar_mode_requested source=tools from=%s to=%s",
                    Esp32BaseWeb::footerBarModeName(),
                    footerBarModeName(mode));
    const bool ok = Esp32BaseWeb::setFooterBarMode(mode);
    redirectSeeOther(ok ? "/esp32base/system?footer_saved=1" : "/esp32base/system?error=footer_bar_save_failed");
}

void handleToolsRebootPost() {
    markRequest();
    if (!ensurePostAllowed("tools_reboot")) {
        return;
    }
    ESP32BASE_LOG_W("web", "restart_requested source=tools");
    Esp32BaseWeb::sendHeader("Rebooting");
    sendChunk("<script>history.replaceState(null,'','/esp32base/system?restarting=1');</script>");
    Esp32BaseWeb::sendPageTitle("Rebooting", "Device restart was accepted.");
    sendChunk("<section class='panel actionpanel'><h2>Restart requested</h2><p class='muted'>Device is restarting. Please wait a few seconds, then reload the System page.</p><div class='actions'><a class='btnlink' href='/esp32base/system'>Reload System</a></div></section>");
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
    redirectSeeOther(ok ? "/esp32base/system?watchdog_trip_reset=1" : "/esp32base/system?error=watchdog_trip_reset_failed");
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
    uint8_t businessStoreCount = 0;
    uint8_t businessStoresReloaded = 0;
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
#if ESP32BASE_ENABLE_RECORD_STORE
    businessStoreCount = businessRecordStoreCount();
    if (mounted) {
        for (uint8_t i = 0; i < businessStoreCount; ++i) {
            Esp32BaseRecordStore* store = businessRecordStoreAt(i);
            Esp32BaseRecordStore::StoreStatus status;
            if (store && store->reload() && store->readStatus(status) && status.ready) {
                ++businessStoresReloaded;
            } else {
                ESP32BASE_LOG_W("web", "business_record_store_reload_failed source=tools_format path=%s state=%s error=%s",
                                store && store->path() ? store->path() : "-",
                                store ? Esp32BaseRecordStore::storeStateName(store->state()) : "missing",
                                store && store->lastErrorReason() ? store->lastErrorReason() : "unavailable");
            }
        }
    }
#endif
    ESP32BASE_LOG_W("web", "fs_format_completed source=tools format=%s mount=%s filelog_reload=%s app_events_recreate=%s business_stores_reload=%u/%u",
                    formatted ? "success" : "failed",
                    mounted ? "success" : "failed",
                    fileLogReloaded ? "success" : "skipped",
                    appEventsRecreated ? "success" : "skipped",
                    static_cast<unsigned>(businessStoresReloaded),
                    static_cast<unsigned>(businessStoreCount));
    if (formatted) {
        notifyToolsFormatFsSuccess(mounted, fileLogReloaded, businessStoreCount, businessStoresReloaded);
    }
    if (formatted && mounted
#if ESP32BASE_ENABLE_APP_EVENTS
        && appEventsRecreated
#endif
        && businessStoresReloaded == businessStoreCount
    ) {
        redirectSeeOther("/esp32base/system?formatted=1");
    } else {
        redirectSeeOther(!formatted ? "/esp32base/system?error=format_failed" :
                         !mounted ? "/esp32base/system?error=mount_failed" :
#if ESP32BASE_ENABLE_APP_EVENTS
                         !appEventsRecreated ? "/esp32base/system?error=app_events_recreate_failed" :
#endif
                         "/esp32base/system?error=business_record_stores_recreate_failed");
    }
#else
    ESP32BASE_LOG_W("web", "fs_format_requested source=tools result=unavailable");
    redirectSeeOther("/esp32base/system?error=fs_unavailable");
#endif
}

void handleToolsLogsClearPost() {
    markRequest();
    if (!ensurePostAllowed("tools_logs_clear")) {
        return;
    }
#if ESP32BASE_ENABLE_FILELOG
    const bool ok = Esp32BaseFileLog::clear();
    redirectSeeOther(ok ? "/esp32base/system?logs_cleared=1" : "/esp32base/system?error=logs_clear_failed");
#else
    redirectSeeOther("/esp32base/system?error=logs_unavailable");
#endif
}

#if ESP32BASE_ENABLE_RECORD_STORE
void handleToolsBusinessRecordsClearPost() {
    markRequest();
    if (!ensurePostAllowed("tools_business_records_clear")) {
        return;
    }
    const uint8_t total = businessRecordStoreCount();
    if (total == 0) {
        ESP32BASE_LOG_W("web", "business_records_clear_rejected reason=no_registered_stores");
        redirectSeeOther("/esp32base/system?business_records_not_ready=1");
        return;
    }
    for (uint8_t i = 0; i < total; ++i) {
        Esp32BaseRecordStore* store = businessRecordStoreAt(i);
        Esp32BaseRecordStore::StoreStatus status;
        if (!store || !store->readStatus(status) || !status.ready) {
            ESP32BASE_LOG_W("web", "business_records_clear_rejected reason=store_not_ready path=%s state=%s",
                            store && store->path() ? store->path() : "-",
                            store ? Esp32BaseRecordStore::storeStateName(store->state()) : "missing");
            redirectSeeOther("/esp32base/system?business_records_not_ready=1");
            return;
        }
    }

    ESP32BASE_LOG_W("web", "business_records_clear_requested source=tools stores=%u",
                    static_cast<unsigned>(total));
    uint8_t cleared = 0;
    uint8_t cleanupWarnings = 0;
    for (uint8_t i = 0; i < total; ++i) {
        Esp32BaseRecordStore* store = businessRecordStoreAt(i);
        const char* path = store && store->path() ? store->path() : "-";
        Esp32BaseRecordStore::StoreStatus beforeStatus;
        Esp32BaseRecordStore::StoreStatus status;
        const bool beforeReadable = store && store->readStatus(beforeStatus);
        const bool clearReturned = beforeReadable && store->clear();
        const bool statusReadable = store && store->readStatus(status);
        const bool logicallyEmpty = clearReturned && statusReadable && status.ready &&
                                    status.recordCount == 0 &&
                                    status.nextRecordId == beforeStatus.nextRecordId;
        if (!logicallyEmpty) {
            ESP32BASE_LOG_W("web", "business_record_store_clear_failed path=%s clear=%s status=%s records=%lu error=%s",
                            path,
                            clearReturned ? "success" : "failed",
                            statusReadable ? Esp32BaseRecordStore::storeStateName(status.state) : "unavailable",
                            static_cast<unsigned long>(statusReadable ? status.recordCount : 0),
                            statusReadable && status.errorReason ? status.errorReason : "status_unavailable");
            break;
        }
        ++cleared;
        if (status.state != Esp32BaseRecordStore::StoreState::Ready) {
            ++cleanupWarnings;
        }
        ESP32BASE_LOG_W("web", "business_record_store_cleared path=%s state=%s next_id=%lu",
                        path,
                        Esp32BaseRecordStore::storeStateName(status.state),
                        static_cast<unsigned long>(status.nextRecordId));
    }

    char location[128];
    if (cleared == total) {
        snprintf(location, sizeof(location),
                 "/esp32base/system?business_records_cleared=%u%s",
                 static_cast<unsigned>(cleared),
                 cleanupWarnings ? "&cleanup_warning=1" : "");
    } else {
        snprintf(location, sizeof(location),
                 "/esp32base/system?business_records_clear_failed=1&cleared=%u&total=%u",
                 static_cast<unsigned>(cleared),
                 static_cast<unsigned>(total));
    }
    ESP32BASE_LOG_W("web", "business_records_clear_completed source=tools cleared=%u total=%u warnings=%u",
                    static_cast<unsigned>(cleared),
                    static_cast<unsigned>(total),
                    static_cast<unsigned>(cleanupWarnings));
    redirectSeeOther(location);
}
#endif

#if ESP32BASE_ENABLE_APP_EVENTS
void handleToolsAppEventsClearPost() {
    markRequest();
    if (!ensurePostAllowed("tools_app_events_clear")) {
        return;
    }
    const bool ok = Esp32BaseAppEvents::clearEventHistory();
    redirectSeeOther(ok ? "/esp32base/system?app_events_cleared=1" : "/esp32base/system?error=app_events_clear_failed");
}
#endif

} // namespace esp32base_web

#endif
