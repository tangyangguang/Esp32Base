#include "../../Esp32BaseProfile.h"

#if ESP32BASE_ENABLE_WEB && ESP32BASE_ENABLE_APP_EVENTS

#include "WebInternal.h"

namespace esp32base_web {
namespace {

enum AppEventTimeMode : uint8_t {
    APP_EVENT_TIME_ALL,
    APP_EVENT_TIME_REAL,
    APP_EVENT_TIME_UPTIME
};

constexpr const char* kAppEventSummaryCountId = "appev-count";

struct AppEventFilter {
    uint8_t level;
    AppEventTimeMode timeMode;
    char source[12];
    char type[24];
    char reason[24];
    char q[48];
    char query[220];
};

uint32_t parseUintArg(const char* name, uint32_t fallback, uint32_t minValue, uint32_t maxValue) {
    if (!name || !g_server.hasArg(name)) {
        return fallback;
    }
    const String raw = g_server.arg(name);
    if (raw.length() == 0 || raw.length() > 10) {
        return fallback;
    }
    uint32_t value = 0;
    for (size_t i = 0; i < raw.length(); ++i) {
        const char c = raw.charAt(i);
        if (c < '0' || c > '9') {
            return fallback;
        }
        value = value * 10U + static_cast<uint32_t>(c - '0');
        if (value > maxValue) {
            return maxValue;
        }
    }
    if (value < minValue) {
        return minValue;
    }
    return value;
}

uint8_t parseLevelArg() {
    if (!g_server.hasArg("level")) {
        return 0;
    }
    const String raw = g_server.arg("level");
    if (raw == "info") {
        return Esp32BaseAppEventLog::LEVEL_INFO;
    }
    if (raw == "warn") {
        return Esp32BaseAppEventLog::LEVEL_WARN;
    }
    if (raw == "error") {
        return Esp32BaseAppEventLog::LEVEL_ERROR;
    }
    return 0;
}

AppEventTimeMode parseTimeModeArg() {
    if (!g_server.hasArg("time")) {
        return APP_EVENT_TIME_ALL;
    }
    const String raw = g_server.arg("time");
    if (raw == "real") {
        return APP_EVENT_TIME_REAL;
    }
    if (raw == "uptime") {
        return APP_EVENT_TIME_UPTIME;
    }
    return APP_EVENT_TIME_ALL;
}

bool validFilterTokenChar(char c) {
    return (c >= 'a' && c <= 'z') ||
           (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') ||
           c == '_' || c == '-' || c == '.' || c == ':' || c == '/' || c == '@' || c == '#';
}

bool validFilterToken(const String& raw, size_t fieldLen) {
    if (raw.length() >= fieldLen) {
        return false;
    }
    for (size_t i = 0; i < raw.length(); ++i) {
        if (!validFilterTokenChar(raw.charAt(i))) {
            return false;
        }
    }
    return true;
}

bool readFilterTokenArg(const char* name, char* out, size_t len) {
    if (!out || len == 0) {
        return false;
    }
    out[0] = '\0';
    if (!name || !g_server.hasArg(name)) {
        return true;
    }
    const String raw = g_server.arg(name);
    if (raw.length() == 0) {
        return true;
    }
    if (!validFilterToken(raw, len)) {
        return false;
    }
    strlcpy(out, raw.c_str(), len);
    return true;
}

bool readKeywordFilterArg(const char* name, char* out, size_t len) {
    if (!out || len == 0) {
        return false;
    }
    out[0] = '\0';
    if (!name || !g_server.hasArg(name)) {
        return true;
    }
    const String raw = g_server.arg(name);
    if (raw.length() == 0) {
        return true;
    }
    if (raw.length() >= len) {
        return false;
    }
    for (size_t i = 0; i < raw.length(); ++i) {
        const uint8_t c = static_cast<uint8_t>(raw.charAt(i));
        if (c < 0x20U || c == 0x7FU) {
            return false;
        }
    }
    strlcpy(out, raw.c_str(), len);
    return true;
}

char asciiLower(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + ('a' - 'A')) : c;
}

bool containsIgnoreCase(const char* text, const char* query) {
    if (!query || !query[0]) {
        return true;
    }
    if (!text || !text[0]) {
        return false;
    }
    const size_t queryLen = strlen(query);
    for (const char* p = text; *p; ++p) {
        size_t i = 0;
        while (i < queryLen && p[i] && asciiLower(p[i]) == asciiLower(query[i])) {
            ++i;
        }
        if (i == queryLen) {
            return true;
        }
    }
    return false;
}

uint32_t resolveAppEventEpoch(const Esp32BaseAppEventRecord& event) {
    if (event.epochSec != 0) {
        return event.epochSec;
    }
#if ESP32BASE_ENABLE_NTP
    uint32_t epoch = 0;
    if (Esp32BaseNtp::resolveCurrentBootEvent(event.bootId, event.uptimeSec, &epoch)) {
        return epoch;
    }
#endif
    return 0;
}

uint64_t appEventUptimeMs(const Esp32BaseAppEventRecord& event) {
    return static_cast<uint64_t>(event.uptimeSec) * 1000ULL;
}

bool appEventHasRealTime(const Esp32BaseAppEventRecord& event) {
    return resolveAppEventEpoch(event) != 0;
}

bool filterMatches(const Esp32BaseAppEventRecord& event, const AppEventFilter& filter) {
    if (filter.level != 0 && event.level != filter.level) {
        return false;
    }
    const bool realTime = appEventHasRealTime(event);
    if (filter.timeMode == APP_EVENT_TIME_REAL && !realTime) {
        return false;
    }
    if (filter.timeMode == APP_EVENT_TIME_UPTIME && realTime) {
        return false;
    }
    if (filter.source[0] && strcmp(event.source, filter.source) != 0) {
        return false;
    }
    if (filter.type[0] && strcmp(event.type, filter.type) != 0) {
        return false;
    }
    if (filter.reason[0] && strcmp(event.reason, filter.reason) != 0) {
        return false;
    }
    if (filter.q[0]) {
        return containsIgnoreCase(event.source, filter.q) ||
               containsIgnoreCase(event.type, filter.q) ||
               containsIgnoreCase(event.reason, filter.q) ||
               containsIgnoreCase(event.object, filter.q) ||
               containsIgnoreCase(event.text, filter.q);
    }
    return true;
}

void appendRaw(char* out, size_t len, const char* value) {
    if (!out || len == 0 || !value) {
        return;
    }
    strlcat(out, value, len);
}

void appendUrlEncoded(char* out, size_t len, const char* value) {
    if (!out || len == 0 || !value) {
        return;
    }
    static const char hex[] = "0123456789ABCDEF";
    for (const char* p = value; *p; ++p) {
        const uint8_t c = static_cast<uint8_t>(*p);
        const bool plain = (c >= 'a' && c <= 'z') ||
                           (c >= 'A' && c <= 'Z') ||
                           (c >= '0' && c <= '9') ||
                           c == '_' || c == '-' || c == '.' || c == ':';
        char tmp[4] = "";
        if (plain) {
            tmp[0] = static_cast<char>(c);
            tmp[1] = '\0';
        } else {
            tmp[0] = '%';
            tmp[1] = hex[(c >> 4) & 0x0F];
            tmp[2] = hex[c & 0x0F];
            tmp[3] = '\0';
        }
        if (strlcat(out, tmp, len) >= len) {
            return;
        }
    }
}

void appendQueryParam(char* out, size_t len, const char* name, const char* value) {
    if (!out || len == 0 || !name || !name[0] || !value || !value[0]) {
        return;
    }
    if (out[0]) {
        appendRaw(out, len, "&");
    }
    appendRaw(out, len, name);
    appendRaw(out, len, "=");
    appendUrlEncoded(out, len, value);
}

void buildFilterQuery(AppEventFilter& filter) {
    filter.query[0] = '\0';
    if (filter.level == Esp32BaseAppEventLog::LEVEL_INFO) {
        appendQueryParam(filter.query, sizeof(filter.query), "level", "info");
    } else if (filter.level == Esp32BaseAppEventLog::LEVEL_WARN) {
        appendQueryParam(filter.query, sizeof(filter.query), "level", "warn");
    } else if (filter.level == Esp32BaseAppEventLog::LEVEL_ERROR) {
        appendQueryParam(filter.query, sizeof(filter.query), "level", "error");
    }
    if (filter.timeMode == APP_EVENT_TIME_REAL) {
        appendQueryParam(filter.query, sizeof(filter.query), "time", "real");
    } else if (filter.timeMode == APP_EVENT_TIME_UPTIME) {
        appendQueryParam(filter.query, sizeof(filter.query), "time", "uptime");
    }
    appendQueryParam(filter.query, sizeof(filter.query), "source", filter.source);
    appendQueryParam(filter.query, sizeof(filter.query), "type", filter.type);
    appendQueryParam(filter.query, sizeof(filter.query), "reason", filter.reason);
    appendQueryParam(filter.query, sizeof(filter.query), "q", filter.q);
}

bool readFilter(AppEventFilter& filter) {
    memset(&filter, 0, sizeof(filter));
    filter.level = parseLevelArg();
    filter.timeMode = parseTimeModeArg();
    if (!readFilterTokenArg("source", filter.source, sizeof(filter.source)) ||
        !readFilterTokenArg("type", filter.type, sizeof(filter.type)) ||
        !readFilterTokenArg("reason", filter.reason, sizeof(filter.reason)) ||
        !readKeywordFilterArg("q", filter.q, sizeof(filter.q))) {
        return false;
    }
    buildFilterQuery(filter);
    return true;
}

void sendInvalidFilterJson() {
    if (!beginResponse(400, "application/json", nullptr)) {
        return;
    }
    sendChunk("{\"ok\":false,\"error\":\"invalid_filter\"}");
    endResponse();
}

void sendInvalidFilterText() {
    g_server.send(400, "text/plain; charset=utf-8", "invalid_filter");
}

bool filterActive(const AppEventFilter& filter) {
    return filter.level != 0 ||
           filter.timeMode != APP_EVENT_TIME_ALL ||
           filter.source[0] ||
           filter.type[0] ||
           filter.reason[0] ||
           filter.q[0];
}

Esp32BaseWeb::UiTone appEventLevelTone(uint8_t level) {
    switch (level) {
        case Esp32BaseAppEventLog::LEVEL_ERROR: return Esp32BaseWeb::UI_DANGER;
        case Esp32BaseAppEventLog::LEVEL_WARN: return Esp32BaseWeb::UI_WARN;
        case Esp32BaseAppEventLog::LEVEL_INFO:
        default: return Esp32BaseWeb::UI_INFO;
    }
}

const char* appEventLevelName(uint8_t level) {
    return Esp32BaseAppEventLog::levelName(static_cast<Esp32BaseAppEventLog::Level>(level));
}

void formatEpoch(uint32_t epoch, char* out, size_t len) {
    if (!out || len == 0) {
        return;
    }
    const time_t raw = static_cast<time_t>(epoch);
    struct tm tmValue;
    localtime_r(&raw, &tmValue);
    if (strftime(out, len, "%Y-%m-%d %H:%M:%S", &tmValue) == 0) {
        strlcpy(out, "-", len);
    }
}

void formatAppEventTime(const Esp32BaseAppEventRecord& event, char* out, size_t len) {
    if (!out || len == 0) {
        return;
    }
    const uint32_t epoch = resolveAppEventEpoch(event);
    if (epoch != 0) {
        formatEpoch(epoch, out, len);
        return;
    }
    snprintf(out, len, "uptime %llu ms", static_cast<unsigned long long>(appEventUptimeMs(event)));
}

void sendFilterLevelOption(const char* value, const char* label, uint8_t level, const AppEventFilter& filter) {
    sendChunk("<option value='");
    sendEscapedHtmlChunk(value);
    sendChunk("'");
    if (filter.level == level) {
        sendChunk(" selected");
    }
    sendChunk(">");
    sendEscapedHtmlChunk(label);
    sendChunk("</option>");
}

void sendFilterTimeOption(const char* value, const char* label, AppEventTimeMode mode, const AppEventFilter& filter) {
    sendChunk("<option value='");
    sendEscapedHtmlChunk(value);
    sendChunk("'");
    if (filter.timeMode == mode) {
        sendChunk(" selected");
    }
    sendChunk(">");
    sendEscapedHtmlChunk(label);
    sendChunk("</option>");
}

void sendFilterInput(const char* label, const char* name, const char* value, const char* cssClass, uint8_t maxLen) {
    sendChunk("<div class='field ");
    sendEscapedHtmlChunk(cssClass);
    sendChunk("'><label>");
    sendEscapedHtmlChunk(label);
    sendChunk("</label><input type='text' name='");
    sendEscapedHtmlChunk(name);
    sendChunk("' value='");
    sendEscapedHtmlChunk(value);
    sendChunk("' maxlength='");
    sendUintChunk(maxLen);
    sendChunk("'></div>");
}

void sendFiltersPanel(const AppEventFilter& filter, uint32_t per) {
    sendChunk("<section class='panel formpanel appevfilters'><h2>Filters</h2><form method='get' action='/esp32base/app-events' class='editform'><input type='hidden' name='per' value='");
    sendUintChunk(per);
    sendChunk("'><div class='fieldgrid'><div class='field short'><label>Level</label><select name='level'>");
    sendFilterLevelOption("", "All", 0, filter);
    sendFilterLevelOption("info", "Info", Esp32BaseAppEventLog::LEVEL_INFO, filter);
    sendFilterLevelOption("warn", "Warn", Esp32BaseAppEventLog::LEVEL_WARN, filter);
    sendFilterLevelOption("error", "Error", Esp32BaseAppEventLog::LEVEL_ERROR, filter);
    sendChunk("</select></div><div class='field short'><label>Time</label><select name='time'>");
    sendFilterTimeOption("", "All", APP_EVENT_TIME_ALL, filter);
    sendFilterTimeOption("real", "Real time", APP_EVENT_TIME_REAL, filter);
    sendFilterTimeOption("uptime", "Uptime", APP_EVENT_TIME_UPTIME, filter);
    sendChunk("</select></div>");
    sendFilterInput("Source", "source", filter.source, "med", sizeof(filter.source) - 1U);
    sendFilterInput("Type", "type", filter.type, "med", sizeof(filter.type) - 1U);
    sendFilterInput("Reason", "reason", filter.reason, "med", sizeof(filter.reason) - 1U);
    sendFilterInput("Keyword", "q", filter.q, "long", sizeof(filter.q) - 1U);
    sendChunk("</div><div class='actions'><input type='submit' value='Apply'><a class='btnlink secondary' href='/esp32base/app-events'>Reset</a></div></form></section>");
}

void sendAppEventsSummary(const AppEventFilter& filter) {
    char count[16];
    char capacity[16];
    snprintf(count, sizeof(count), "%u", static_cast<unsigned>(Esp32BaseAppEventLog::count()));
    snprintf(capacity, sizeof(capacity), "%u", static_cast<unsigned>(Esp32BaseAppEventLog::capacity()));
    Esp32BaseWeb::beginMetricGrid();
    sendChunk("<div class='metric'><b id='");
    sendChunk(kAppEventSummaryCountId);
    sendChunk("'>");
    sendEscapedHtmlChunk(count);
    sendChunk("</b><span>Events</span></div>");
    Esp32BaseWeb::sendMetric("Capacity", capacity);
    Esp32BaseWeb::sendMetric("Path", Esp32BaseAppEventLog::path());
    Esp32BaseWeb::sendMetric("Filter", filterActive(filter) ? "Active" : "All");
    Esp32BaseWeb::endMetricGrid();
}

void syncAppEventsSummaryCount() {
    sendChunk("<script>(function(){var e=document.getElementById('");
    sendChunk(kAppEventSummaryCountId);
    sendChunk("');if(e)e.textContent='");
    sendUintChunk(Esp32BaseAppEventLog::count());
    sendChunk("';})();</script>");
}

void sendCsvNumber(uint64_t value) {
    char buf[24];
    snprintf(buf, sizeof(buf), "%llu", static_cast<unsigned long long>(value));
    sendChunk(buf);
}

void sendCsvInt(int32_t value) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%ld", static_cast<long>(value));
    sendChunk(buf);
}

void sendCsvMaybeInt(const Esp32BaseAppEventRecord& event, uint8_t mask, int32_t value) {
    if (event.valueMask & mask) {
        sendCsvInt(value);
    }
}

void sendAppEventValueJson(const Esp32BaseAppEventRecord& event, uint8_t mask, int32_t value) {
    if (event.valueMask & mask) {
        sendIntChunk(value);
    } else {
        sendChunk("null");
    }
}

void sendValueChip(const Esp32BaseAppEventRecord& event, uint8_t mask, const char* label, int32_t value) {
    if (!(event.valueMask & mask)) {
        return;
    }
    sendChunk("<span class='evchip'>");
    sendEscapedHtmlChunk(label);
    sendChunk(" ");
    sendIntChunk(value);
    sendChunk("</span>");
}

void sendAppEventJson(const Esp32BaseAppEventRecord& event) {
    const uint32_t resolvedEpoch = resolveAppEventEpoch(event);
    sendChunk("{\"id\":");
    sendUintChunk(event.id);
    sendChunk(",\"epochSec\":");
    sendUintChunk(event.epochSec);
    sendChunk(",\"resolvedEpochSec\":");
    sendUintChunk(resolvedEpoch);
    sendChunk(",\"bootId\":");
    sendUintChunk(event.bootId);
    sendChunk(",\"uptimeSec\":");
    sendUintChunk(event.uptimeSec);
    sendChunk(",\"uptimeMs\":");
    sendUintChunk(appEventUptimeMs(event));
    sendChunk(",\"level\":\"");
    sendEscapedJsonChunk(appEventLevelName(event.level));
    sendChunk("\",\"source\":\"");
    sendEscapedJsonChunk(event.source);
    sendChunk("\",\"type\":\"");
    sendEscapedJsonChunk(event.type);
    sendChunk("\",\"reason\":\"");
    sendEscapedJsonChunk(event.reason);
    sendChunk("\",\"object\":\"");
    sendEscapedJsonChunk(event.object);
    sendChunk("\",\"code\":");
    sendUintChunk(event.code);
    sendChunk(",\"value1\":");
    sendAppEventValueJson(event, Esp32BaseAppEventLog::VALUE1, event.value1);
    sendChunk(",\"value2\":");
    sendAppEventValueJson(event, Esp32BaseAppEventLog::VALUE2, event.value2);
    sendChunk(",\"value3\":");
    sendAppEventValueJson(event, Esp32BaseAppEventLog::VALUE3, event.value3);
    sendChunk(",\"valueMask\":");
    sendUintChunk(event.valueMask);
    sendChunk(",\"flags\":");
    sendUintChunk(event.flags);
    sendChunk(",\"text\":\"");
    sendEscapedJsonChunk(event.text);
    sendChunk("\"}");
}

struct AppEventScanState {
    const AppEventFilter* filter;
    uint32_t offset;
    uint32_t limit;
    uint32_t matched;
    uint32_t rows;
};

void sendAppEventHtmlRow(const Esp32BaseAppEventRecord& event, void* user) {
    AppEventScanState* state = static_cast<AppEventScanState*>(user);
    if (!state || !state->filter || !filterMatches(event, *state->filter)) {
        return;
    }
    if (state->matched++ < state->offset || state->rows >= state->limit) {
        return;
    }
    ++state->rows;

    char timeText[32];
    formatAppEventTime(event, timeText, sizeof(timeText));
    sendChunk("<tr><td class='evid'>");
    sendUintChunk(event.id);
    sendChunk("</td><td class='evtime'><span>");
    sendEscapedHtmlChunk(timeText);
    sendChunk("</span><small>boot ");
    sendUintChunk(event.bootId);
    sendChunk("</small></td><td>");
    sendStatusTag(appEventLevelTone(event.level), appEventLevelName(event.level));
    sendChunk("</td><td><div class='evmain'>");
    sendEscapedHtmlChunk(event.source);
    sendChunk("</div><div class='evsub'>");
    sendEscapedHtmlChunk(event.type);
    if (event.reason[0]) {
        sendChunk(" / ");
        sendEscapedHtmlChunk(event.reason);
    }
    sendChunk("</div></td><td class='evobject'>");
    sendEscapedHtmlChunk(event.object[0] ? event.object : "-");
    sendChunk("</td><td class='evdetail'><div class='evtext'>");
    sendEscapedHtmlChunk(event.text[0] ? event.text : "-");
    sendChunk("</div><div class='evchips'><span class='evchip'>code ");
    sendUintChunk(event.code);
    sendChunk("</span>");
    sendValueChip(event, Esp32BaseAppEventLog::VALUE1, "v1", event.value1);
    sendValueChip(event, Esp32BaseAppEventLog::VALUE2, "v2", event.value2);
    sendValueChip(event, Esp32BaseAppEventLog::VALUE3, "v3", event.value3);
    sendChunk("</div></td></tr>");
}

void sendAppEventJsonRow(const Esp32BaseAppEventRecord& event, void* user) {
    AppEventScanState* state = static_cast<AppEventScanState*>(user);
    if (!state || !state->filter || !filterMatches(event, *state->filter)) {
        return;
    }
    if (state->matched++ < state->offset || state->rows >= state->limit) {
        return;
    }
    if (state->rows > 0) {
        sendChunk(",");
    }
    sendAppEventJson(event);
    ++state->rows;
}

struct AppEventCsvState {
    const AppEventFilter* filter;
    uint32_t matched;
};

void sendAppEventCsvRow(const Esp32BaseAppEventRecord& event, void* user) {
    AppEventCsvState* state = static_cast<AppEventCsvState*>(user);
    if (!state || !state->filter || !filterMatches(event, *state->filter)) {
        return;
    }
    ++state->matched;
    sendCsvNumber(event.id);
    sendChunk(",");
    sendCsvNumber(event.epochSec);
    sendChunk(",");
    sendCsvNumber(resolveAppEventEpoch(event));
    sendChunk(",");
    sendCsvNumber(event.bootId);
    sendChunk(",");
    sendCsvNumber(event.uptimeSec);
    sendChunk(",");
    sendCsvNumber(appEventUptimeMs(event));
    sendChunk(",");
    Esp32BaseWeb::writeCsvEscaped(appEventLevelName(event.level));
    sendChunk(",");
    Esp32BaseWeb::writeCsvEscaped(event.source);
    sendChunk(",");
    Esp32BaseWeb::writeCsvEscaped(event.type);
    sendChunk(",");
    Esp32BaseWeb::writeCsvEscaped(event.reason);
    sendChunk(",");
    Esp32BaseWeb::writeCsvEscaped(event.object);
    sendChunk(",");
    sendCsvNumber(event.code);
    sendChunk(",");
    sendCsvMaybeInt(event, Esp32BaseAppEventLog::VALUE1, event.value1);
    sendChunk(",");
    sendCsvMaybeInt(event, Esp32BaseAppEventLog::VALUE2, event.value2);
    sendChunk(",");
    sendCsvMaybeInt(event, Esp32BaseAppEventLog::VALUE3, event.value3);
    sendChunk(",");
    Esp32BaseWeb::writeCsvEscaped(event.text);
    sendChunk("\n");
}

void sendFilterJson(const AppEventFilter& filter) {
    sendChunk("\"filters\":{\"level\":\"");
    if (filter.level) {
        sendEscapedJsonChunk(appEventLevelName(filter.level));
    }
    sendChunk("\",\"time\":\"");
    sendEscapedJsonChunk(filter.timeMode == APP_EVENT_TIME_REAL ? "real" : (filter.timeMode == APP_EVENT_TIME_UPTIME ? "uptime" : ""));
    sendChunk("\",\"source\":\"");
    sendEscapedJsonChunk(filter.source);
    sendChunk("\",\"type\":\"");
    sendEscapedJsonChunk(filter.type);
    sendChunk("\",\"reason\":\"");
    sendEscapedJsonChunk(filter.reason);
    sendChunk("\",\"q\":\"");
    sendEscapedJsonChunk(filter.q);
    sendChunk("\"}");
}

void sendCsvLink(const AppEventFilter& filter) {
    sendChunk("<a class='btnlink' href='/esp32base/app-events.csv");
    if (filter.query[0]) {
        sendChunk("?");
        sendEscapedHtmlChunk(filter.query);
    }
    sendChunk("'>Export CSV</a>");
}

} // namespace

void handleAppEventsPage() {
    markRequest();
    if (!ensureAuth()) {
        return;
    }
    AppEventFilter filter;
    if (!readFilter(filter)) {
        sendInvalidFilterText();
        return;
    }
    const uint32_t per = parseUintArg("per", 20, 1, 100);
    const uint32_t page = parseUintArg("page", 1, 1, 65535);

    Esp32BaseWeb::sendHeader(g_builtinLabels[Esp32BaseWeb::BUILTIN_APP_EVENTS]);
    Esp32BaseWeb::sendPageTitle(g_builtinLabels[Esp32BaseWeb::BUILTIN_APP_EVENTS], "Application event log");
    if (g_server.hasArg("cleared")) {
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_OK, "App Events cleared");
    } else if (g_server.hasArg("error")) {
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_DANGER, "App Events action failed", g_server.arg("error").c_str());
    }
    if (Esp32BaseAppEventLog::faulted()) {
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_DANGER, "App Events storage fault", Esp32BaseAppEventLog::lastError());
    } else if (!Esp32BaseAppEventLog::isReady()) {
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_WARN, "App Events unavailable", Esp32BaseAppEventLog::lastError());
    }
    sendAppEventsSummary(filter);
    sendFiltersPanel(filter, per);

    sendChunk("<section class='panel actionpanel'><h2>Actions</h2><div class='actions'>");
    sendCsvLink(filter);
    sendChunk("<form method='post' action='/esp32base/app-events/clear' onsubmit=\"return confirm('Clear App Events?')&&once(this)\"><input class='danger' type='submit' value='Clear App Events'></form></div></section>");

    uint32_t offset = (page - 1U) * per;

    sendChunk("<section class='panel'><h2>Events</h2><div class='tablewrap'><table class='evtable'><tr><th>ID</th><th>Time</th><th>Level</th><th>Event</th><th>Object</th><th>Details</th></tr>");
    AppEventScanState state = {&filter, offset, per, 0, 0};
    const bool readOk = Esp32BaseAppEventLog::readLatest(0,
                                                         Esp32BaseAppEventLog::count(),
                                                         sendAppEventHtmlRow,
                                                         &state);
    if (!readOk) {
        sendChunk("<tr><td colspan='6'>App Events unavailable: ");
        sendEscapedHtmlChunk(Esp32BaseAppEventLog::lastError());
        sendChunk("</td></tr>");
    } else if (state.rows == 0) {
        sendChunk("<tr><td colspan='6'>No App Events</td></tr>");
    }
    sendChunk("</table></div></section>");
    syncAppEventsSummaryCount();
    Esp32BaseWeb::Pagination pagination = {"/esp32base/app-events", filter.query, page, per, state.matched};
    Esp32BaseWeb::sendPagination(pagination);
    Esp32BaseWeb::sendFooter();
}

void handleAppEventsApi() {
    markRequest();
    if (!ensureAuth()) {
        return;
    }
    // Route marker: /esp32base/api/app-events
    AppEventFilter filter;
    if (!readFilter(filter)) {
        sendInvalidFilterJson();
        return;
    }
    const uint32_t offset = parseUintArg("offset", 0, 0, 65535);
    const uint32_t limit = parseUintArg("limit", 50, 1, 100);
    if (!beginResponse(200, "application/json", nullptr)) {
        return;
    }
    sendChunk("{\"kind\":\"app_events\",\"path\":\"/esp32base/api/app-events\",\"capacity\":");
    sendUintChunk(Esp32BaseAppEventLog::capacity());
    sendChunk(",\"offset\":");
    sendUintChunk(offset);
    sendChunk(",\"limit\":");
    sendUintChunk(limit);
    sendChunk(",\"faulted\":");
    sendChunk(Esp32BaseAppEventLog::faulted() ? "true" : "false");
    sendChunk(",");
    sendFilterJson(filter);
    sendChunk(",\"events\":[");
    AppEventScanState state = {&filter, offset, limit, 0, 0};
    const bool readOk = Esp32BaseAppEventLog::readLatest(0,
                                                        Esp32BaseAppEventLog::count(),
                                                        sendAppEventJsonRow,
                                                        &state);
    sendChunk("],\"count\":");
    sendUintChunk(Esp32BaseAppEventLog::count());
    sendChunk(",\"total\":");
    sendUintChunk(state.matched);
    sendChunk(",\"readOk\":");
    sendChunk(readOk ? "true" : "false");
    sendChunk(",\"ok\":");
    sendChunk((readOk && !Esp32BaseAppEventLog::faulted()) ? "true" : "false");
    sendChunk(",\"lastError\":\"");
    sendEscapedJsonChunk(Esp32BaseAppEventLog::lastError());
    sendChunk("\"}");
    endResponse();
}

void handleAppEventsCsv() {
    markRequest();
    if (!ensureAuth()) {
        return;
    }
    AppEventFilter filter;
    if (!readFilter(filter)) {
        sendInvalidFilterText();
        return;
    }
    if (!Esp32BaseWeb::beginCsv(200, "app-events.csv")) {
        return;
    }
    sendChunk("id,epoch_sec,resolved_epoch_sec,boot_id,uptime_sec,uptime_ms,level,source,type,reason,object,code,value1,value2,value3,text\n");
    AppEventCsvState state = {&filter, 0};
    const bool readOk = Esp32BaseAppEventLog::readLatest(0, Esp32BaseAppEventLog::count(), sendAppEventCsvRow, &state);
    if (!readOk) {
        ESP32BASE_LOG_W("app_events", "csv_export_incomplete error=%s", Esp32BaseAppEventLog::lastError());
        sendChunk("# error,");
        Esp32BaseWeb::writeCsvEscaped(Esp32BaseAppEventLog::lastError());
        sendChunk("\n");
    }
    endResponse();
}

void handleAppEventsClearPost() {
    markRequest();
    if (!ensurePostAllowed("app_events_clear")) {
        return;
    }
    const bool ok = Esp32BaseAppEventLog::clear();
    redirectSeeOther(ok ? "/esp32base/app-events?cleared=1" : "/esp32base/app-events?error=clear_failed");
}

} // namespace esp32base_web

#endif
