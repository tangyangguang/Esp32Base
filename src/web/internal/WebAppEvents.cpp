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

size_t boundedTextLen(const char* text, size_t maxLen) {
    if (!text) {
        return 0;
    }
    size_t n = 0;
    while (n < maxLen && text[n] != '\0') {
        ++n;
    }
    return n;
}

void copyBoundedText(char* out, size_t outLen, const char* text, size_t maxLen) {
    if (!out || outLen == 0) {
        return;
    }
    const size_t n = boundedTextLen(text, maxLen);
    const size_t copyLen = n < outLen - 1U ? n : outLen - 1U;
    if (copyLen > 0) {
        memcpy(out, text, copyLen);
    }
    out[copyLen] = '\0';
}

void sendBoundedHtmlChunk(const char* text, size_t maxLen) {
    char tmp[72];
    copyBoundedText(tmp, sizeof(tmp), text, maxLen);
    sendEscapedHtmlChunk(tmp);
}

void writeBoundedCsvEscaped(const char* text, size_t maxLen) {
    char tmp[72];
    copyBoundedText(tmp, sizeof(tmp), text, maxLen);
    Esp32BaseWeb::writeCsvEscaped(tmp);
}

bool boundedEquals(const char* text, size_t maxLen, const char* expected) {
    if (!expected || !expected[0]) {
        return true;
    }
    char tmp[72];
    copyBoundedText(tmp, sizeof(tmp), text, maxLen);
    return strcmp(tmp, expected) == 0;
}

bool boundedContainsIgnoreCase(const char* text, size_t maxLen, const char* query) {
    if (!query || !query[0]) {
        return true;
    }
    char tmp[72];
    copyBoundedText(tmp, sizeof(tmp), text, maxLen);
    return containsIgnoreCase(tmp, query);
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

bool filterMatches(const Esp32BaseAppEventLog::StoreRecord& item, const AppEventFilter& filter) {
    if (!item.readOk) {
        return false;
    }
    const Esp32BaseAppEventRecord& event = item.record;
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
    if (!boundedEquals(event.source, sizeof(event.source), filter.source) ||
        !boundedEquals(event.type, sizeof(event.type), filter.type) ||
        !boundedEquals(event.reason, sizeof(event.reason), filter.reason)) {
        return false;
    }
    if (filter.q[0]) {
        return boundedContainsIgnoreCase(event.source, sizeof(event.source), filter.q) ||
               boundedContainsIgnoreCase(event.type, sizeof(event.type), filter.q) ||
               boundedContainsIgnoreCase(event.reason, sizeof(event.reason), filter.q) ||
               boundedContainsIgnoreCase(event.object, sizeof(event.object), filter.q) ||
               boundedContainsIgnoreCase(event.text, sizeof(event.text), filter.q);
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

void sendCsvLink(const AppEventFilter& filter);

void sendFiltersPanel(const AppEventFilter& filter, uint32_t per) {
    sendChunk("<section class='panel formpanel appevfilters'><h2>Filter &amp; Export</h2><form method='get' action='/esp32base/app-events' class='editform'><input type='hidden' name='per' value='");
    sendUintChunk(per);
    sendChunk("'><div class='fieldgrid appevfiltergrid'><div class='field short'><label>Level</label><select name='level'>");
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
    sendChunk("</div><div class='actions appevactions'><input type='submit' value='Apply'><a class='btnlink secondary' href='/esp32base/app-events'>Reset</a>");
    sendCsvLink(filter);
    sendChunk("</div></form></section>");
}

void sendAppEventsSummary(const Esp32BaseAppEventLog::StoreInfo& info) {
    char events[24];
    char fileSize[32];
    char capacity[24];
    char nextId[24];
    snprintf(events, sizeof(events), "%u / %u",
             static_cast<unsigned>(info.validCount),
             static_cast<unsigned>(info.count));
    if (info.fileSize >= 0) {
        formatReadableBytes(static_cast<uint64_t>(info.fileSize), fileSize, sizeof(fileSize));
    } else {
        strlcpy(fileSize, "-", sizeof(fileSize));
    }
    snprintf(capacity, sizeof(capacity), "%u", static_cast<unsigned>(info.capacity));
    snprintf(nextId, sizeof(nextId), "%lu", static_cast<unsigned long>(info.nextId));
    Esp32BaseWeb::beginMetricGrid();
    Esp32BaseWeb::sendMetric("Valid / total", events);
    Esp32BaseWeb::sendMetric("File", fileSize);
    Esp32BaseWeb::sendMetric("Capacity", capacity);
    Esp32BaseWeb::sendMetric("Next ID", nextId);
    Esp32BaseWeb::endMetricGrid();
    sendChunk("<section class='panel appestore'><h2>Store</h2><div class='submetrics'>");
    sendChunk("<span><b>Path</b><em>");
    sendEscapedHtmlChunk(info.path ? info.path : Esp32BaseAppEventLog::path());
    sendChunk("</em></span><span><b>Expected size</b><em>");
    char expected[32];
    formatReadableBytes(info.expectedFileSize, expected, sizeof(expected));
    sendEscapedHtmlChunk(expected);
    sendChunk("</em></span><span><b>Head</b><em>");
    sendUintChunk(info.head);
    sendChunk("</em></span><span><b>Active header</b><em>");
    sendUintChunk(info.activeHeader);
    sendChunk("</em></span><span><b>Sequence</b><em>");
    sendUintChunk(info.sequence);
    sendChunk("</em></span><span><b>Record size</b><em>");
    sendUintChunk(info.recordSize);
    sendChunk(" B</em></span></div></section>");
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

Esp32BaseWeb::UiTone appEventStoreStatusTone(Esp32BaseAppEventLog::StoreRecordStatus status) {
    switch (status) {
        case Esp32BaseAppEventLog::STORE_RECORD_OK: return Esp32BaseWeb::UI_OK;
        case Esp32BaseAppEventLog::STORE_RECORD_EMPTY: return Esp32BaseWeb::UI_INFO;
        case Esp32BaseAppEventLog::STORE_RECORD_READ_FAILED:
        case Esp32BaseAppEventLog::STORE_RECORD_INVALID_MAGIC:
            return Esp32BaseWeb::UI_DANGER;
        case Esp32BaseAppEventLog::STORE_RECORD_INVALID_LEVEL:
        case Esp32BaseAppEventLog::STORE_RECORD_CRC_MISMATCH:
        case Esp32BaseAppEventLog::STORE_RECORD_UNCOMMITTED:
        default:
            return Esp32BaseWeb::UI_WARN;
    }
}

void formatHex32(uint32_t value, char* out, size_t len) {
    snprintf(out, len, "0x%08lx", static_cast<unsigned long>(value));
}

void formatHex16(uint16_t value, char* out, size_t len) {
    snprintf(out, len, "0x%04x", static_cast<unsigned>(value));
}

void sendDetailRowText(const char* label, const char* value, const char* help = nullptr) {
    sendChunk("<div><b>");
    sendEscapedHtmlChunk(label);
    sendChunk("</b><code>");
    sendEscapedHtmlChunk(value ? value : "");
    sendChunk("</code>");
    if (help && help[0]) {
        sendChunk("<small>");
        sendEscapedHtmlChunk(help);
        sendChunk("</small>");
    }
    sendChunk("</div>");
}

void sendDetailRowUint(const char* label, uint64_t value, const char* help = nullptr) {
    char buf[24];
    snprintf(buf, sizeof(buf), "%llu", static_cast<unsigned long long>(value));
    sendDetailRowText(label, buf, help);
}

void sendDetailRowInt(const char* label, int32_t value, const char* help = nullptr) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%ld", static_cast<long>(value));
    sendDetailRowText(label, buf, help);
}

void sendDetailRowBool(const char* label, bool value, const char* help = nullptr) {
    sendDetailRowText(label, value ? "true" : "false", help);
}

void sendDetailRowBounded(const char* label, const char* value, size_t len, const char* help = nullptr) {
    char tmp[72];
    copyBoundedText(tmp, sizeof(tmp), value, len);
    sendDetailRowText(label, tmp, help);
}

void sendDetailGroupStart(const char* title, const char* help) {
    sendChunk("<section class='evdetailgroup'><h3>");
    sendEscapedHtmlChunk(title);
    sendChunk("</h3>");
    if (help && help[0]) {
        sendChunk("<p>");
        sendEscapedHtmlChunk(help);
        sendChunk("</p>");
    }
    sendChunk("<div class='evdetailgrid'>");
}

void sendDetailGroupEnd() {
    sendChunk("</div></section>");
}

void sendAppEventDetailDialog(const Esp32BaseAppEventLog::StoreRecord& item, uint32_t dialogId) {
    const Esp32BaseAppEventRecord& event = item.record;
    char id[18];
    snprintf(id, sizeof(id), "appev-%lu", static_cast<unsigned long>(dialogId));
    char magicHex[12];
    char crcHex[8];
    char calcCrcHex[8];
    formatHex32(event.magic, magicHex, sizeof(magicHex));
    formatHex16(item.storedCrc16, crcHex, sizeof(crcHex));
    formatHex16(item.calculatedCrc16, calcCrcHex, sizeof(calcCrcHex));
    sendChunk("<dialog id='");
    sendEscapedHtmlChunk(id);
    sendChunk("' class='panel eb-modal evdialog' data-eb-light-dismiss='1'><h2>Event record details</h2>");
    sendDetailGroupStart("Store state", "How this record sits in the fixed event store.");
    sendDetailRowText("status", Esp32BaseAppEventLog::storeRecordStatusName(item.status), "Overall record state derived from read, magic, level, crc and commit checks.");
    sendDetailRowUint("slot", item.slot, "Physical ring-buffer slot.");
    sendDetailRowUint("index", item.index, "Newest-first index in the current header range.");
    sendDetailRowUint("recordOffset", item.offset, "Byte offset inside /app/events.bin.");
    sendDetailGroupEnd();
    sendDetailGroupStart("Validation", "Internal checks used to decide whether the record is a valid business event.");
    sendDetailRowBool("readOk", item.readOk, "Record bytes were read from the file.");
    sendDetailRowBool("magicOk", item.magicOk, "Record magic matches the App Events record signature.");
    sendDetailRowBool("levelOk", item.levelOk, "Level is one of info/warn/error.");
    sendDetailRowBool("crcOk", item.crcOk, "Stored crc16 matches the calculated crc16.");
    sendDetailRowBool("committed", item.committed, "Record id is older than nextId from the active header.");
    sendDetailRowText("crc16", crcHex, "CRC stored in the record.");
    sendDetailRowText("calculatedCrc16", calcCrcHex, "CRC calculated from the bytes currently readable.");
    sendDetailGroupEnd();
    sendDetailGroupStart("Time and identity", "Stable identifiers and time fields stored with the event.");
    sendDetailRowText("magic", magicHex, "Low-level record signature.");
    sendDetailRowUint("id", event.id, "Monotonic event id.");
    sendDetailRowUint("epochSec", event.epochSec, "Trusted epoch written at append time, or 0 when unavailable.");
    sendDetailRowUint("resolvedEpochSec", resolveAppEventEpoch(event), "Epoch resolved later from current boot mapping, when possible.");
    sendDetailRowUint("bootId", event.bootId, "Boot session id used with uptime for relative time.");
    sendDetailRowUint("uptimeSec", event.uptimeSec, "Seconds since boot when the event was written.");
    sendDetailRowUint("uptimeMs", appEventUptimeMs(event), "Derived milliseconds from uptimeSec.");
    sendDetailGroupEnd();
    sendDetailGroupStart("Event fields", "Application-defined fields. Esp32Base stores them but does not interpret business meaning.");
    sendDetailRowUint("level", event.level);
    sendDetailRowText("levelName", appEventLevelName(event.level));
    sendDetailRowBounded("source", event.source, sizeof(event.source), "Required producer token.");
    sendDetailRowBounded("type", event.type, sizeof(event.type), "Required event type token.");
    sendDetailRowBounded("reason", event.reason, sizeof(event.reason), "Optional reason token.");
    sendDetailRowBounded("object", event.object, sizeof(event.object), "Optional business object reference.");
    sendDetailRowUint("code", event.code, "Application-defined short numeric code.");
    sendDetailRowBounded("text", event.text, sizeof(event.text), "Short text, UTF-8 safe truncated when needed.");
    sendDetailGroupEnd();
    sendDetailGroupStart("Values and flags", "Compact numeric payload and internal bit fields.");
    sendDetailRowInt("value1", event.value1);
    sendDetailRowInt("value2", event.value2);
    sendDetailRowInt("value3", event.value3);
    sendDetailRowUint("valueMask", event.valueMask, "Bit mask: bit0=value1, bit1=value2, bit2=value3 are meaningful.");
    sendDetailRowUint("flags", event.flags, "Bit flags such as time synced and text truncated.");
    sendDetailRowUint("reserved", event.reserved, "Reserved byte; should normally be 0.");
    sendDetailGroupEnd();
    sendChunk("<form method='dialog' class='actions'><button class='secondary'>Close</button></form></dialog>");
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

void sendAppEventHtmlRow(const Esp32BaseAppEventLog::StoreRecord& item, void* user) {
    AppEventScanState* state = static_cast<AppEventScanState*>(user);
    if (!state || !state->filter || !filterMatches(item, *state->filter)) {
        return;
    }
    const uint32_t matchedIndex = state->matched++;
    if (matchedIndex < state->offset || state->rows >= state->limit) {
        return;
    }
    ++state->rows;

    const Esp32BaseAppEventRecord& event = item.record;
    char timeText[32];
    formatAppEventTime(event, timeText, sizeof(timeText));
    sendChunk("<tr><td>");
    sendStatusTag(appEventStoreStatusTone(item.status), Esp32BaseAppEventLog::storeRecordStatusName(item.status));
    sendChunk("<small class='evsub'>slot ");
    sendUintChunk(item.slot);
    sendChunk("</small></td><td class='evid'>");
    sendUintChunk(event.id);
    sendChunk("<small class='evsub'>idx ");
    sendUintChunk(item.index);
    sendChunk("</small></td><td class='evtime'><span>");
    sendEscapedHtmlChunk(timeText);
    sendChunk("</span><small>boot ");
    sendUintChunk(event.bootId);
    sendChunk(" / uptime ");
    sendUintChunk(event.uptimeSec);
    sendChunk("s</small></td><td>");
    sendStatusTag(appEventLevelTone(event.level), appEventLevelName(event.level));
    sendChunk("</td><td><div class='evmain'>");
    sendBoundedHtmlChunk(event.source, sizeof(event.source));
    sendChunk("</div><div class='evsub'>");
    sendBoundedHtmlChunk(event.type, sizeof(event.type));
    if (boundedTextLen(event.reason, sizeof(event.reason)) > 0) {
        sendChunk(" / ");
        sendBoundedHtmlChunk(event.reason, sizeof(event.reason));
    }
    sendChunk("</div></td><td class='evobject'>");
    if (boundedTextLen(event.object, sizeof(event.object)) > 0) {
        sendBoundedHtmlChunk(event.object, sizeof(event.object));
    } else {
        sendChunk("-");
    }
    sendChunk("<small class='evsub'>code ");
    sendUintChunk(event.code);
    sendChunk("</small></td><td class='evdetail'><div class='evtext'>");
    if (boundedTextLen(event.text, sizeof(event.text)) > 0) {
        sendBoundedHtmlChunk(event.text, sizeof(event.text));
    } else {
        sendChunk("-");
    }
    sendChunk("</div><div class='evchips'><span class='evchip'>code ");
    sendUintChunk(event.code);
    sendChunk("</span><span class='evchip'>mask ");
    sendUintChunk(event.valueMask);
    sendChunk("</span>");
    sendValueChip(event, Esp32BaseAppEventLog::VALUE1, "v1", event.value1);
    sendValueChip(event, Esp32BaseAppEventLog::VALUE2, "v2", event.value2);
    sendValueChip(event, Esp32BaseAppEventLog::VALUE3, "v3", event.value3);
    sendChunk("</div></td><td>");
    char dialogId[18];
    snprintf(dialogId, sizeof(dialogId), "appev-%lu", static_cast<unsigned long>(matchedIndex + 1U));
    sendChunk("<button type='button' class='btnlink compact' onclick=\"document.getElementById('");
    sendEscapedHtmlChunk(dialogId);
    sendChunk("').showModal()\">Details</button>");
    sendAppEventDetailDialog(item, matchedIndex + 1U);
    sendChunk("</td></tr>");
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

void sendAppEventCsvRow(const Esp32BaseAppEventLog::StoreRecord& item, void* user) {
    AppEventCsvState* state = static_cast<AppEventCsvState*>(user);
    if (!state || !state->filter || !filterMatches(item, *state->filter)) {
        return;
    }
    const Esp32BaseAppEventRecord& event = item.record;
    ++state->matched;
    sendCsvNumber(item.slot);
    sendChunk(",");
    Esp32BaseWeb::writeCsvEscaped(Esp32BaseAppEventLog::storeRecordStatusName(item.status));
    sendChunk(",");
    sendCsvNumber(item.offset);
    sendChunk(",");
    sendCsvNumber(item.storedCrc16);
    sendChunk(",");
    sendCsvNumber(item.calculatedCrc16);
    sendChunk(",");
    sendChunk(item.crcOk ? "1" : "0");
    sendChunk(",");
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
    writeBoundedCsvEscaped(event.source, sizeof(event.source));
    sendChunk(",");
    writeBoundedCsvEscaped(event.type, sizeof(event.type));
    sendChunk(",");
    writeBoundedCsvEscaped(event.reason, sizeof(event.reason));
    sendChunk(",");
    writeBoundedCsvEscaped(event.object, sizeof(event.object));
    sendChunk(",");
    sendCsvNumber(event.code);
    sendChunk(",");
    sendCsvInt(event.value1);
    sendChunk(",");
    sendCsvInt(event.value2);
    sendChunk(",");
    sendCsvInt(event.value3);
    sendChunk(",");
    sendCsvNumber(event.valueMask);
    sendChunk(",");
    sendCsvNumber(event.flags);
    sendChunk(",");
    sendCsvNumber(event.reserved);
    sendChunk(",");
    writeBoundedCsvEscaped(event.text, sizeof(event.text));
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
    const uint32_t per = parseUintArg("per", 10, 1, 100);
    const uint32_t page = parseUintArg("page", 1, 1, 65535);
    Esp32BaseAppEventLog::StoreInfo storeInfo;
    Esp32BaseAppEventLog::readStoreInfo(storeInfo);

    Esp32BaseWeb::sendHeader(g_builtinLabels[Esp32BaseWeb::BUILTIN_APP_EVENTS]);
    Esp32BaseWeb::sendPageTitle(g_builtinLabels[Esp32BaseWeb::BUILTIN_APP_EVENTS], "Application event log and store view.");
    if (g_server.hasArg("error")) {
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_DANGER, "App Events action failed", g_server.arg("error").c_str());
    }
    if (Esp32BaseAppEventLog::faulted()) {
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_DANGER, "App Events storage fault", Esp32BaseAppEventLog::lastError());
    } else if (!Esp32BaseAppEventLog::isReady()) {
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_WARN, "App Events unavailable", Esp32BaseAppEventLog::lastError());
    }
    sendAppEventsSummary(storeInfo);
    sendFiltersPanel(filter, per);

    uint32_t offset = (page - 1U) * per;

    sendChunk("<section class='panel'><h2>Events</h2><div class='tablewrap'><table class='evtable'><tr><th>Status</th><th>ID</th><th>Time</th><th>Level</th><th>Event</th><th>Object</th><th>Details</th><th>Action</th></tr>");
    AppEventScanState state = {&filter, offset, per, 0, 0};
    const bool readOk = Esp32BaseAppEventLog::readStoreRecords(0,
                                                               storeInfo.count,
                                                               sendAppEventHtmlRow,
                                                               &state);
    if (!readOk) {
        sendChunk("<tr><td colspan='8'>App Events unavailable: ");
        sendEscapedHtmlChunk(Esp32BaseAppEventLog::lastError());
        sendChunk("</td></tr>");
    } else if (state.rows == 0) {
        sendChunk("<tr><td colspan='8'>No App Events</td></tr>");
    }
    sendChunk("</table></div></section>");
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
    sendChunk("slot,status,record_offset,stored_crc16,calculated_crc16,crc_ok,id,epoch_sec,resolved_epoch_sec,boot_id,uptime_sec,uptime_ms,level,source,type,reason,object,code,value1,value2,value3,value_mask,flags,reserved,text\n");
    AppEventCsvState state = {&filter, 0};
    Esp32BaseAppEventLog::StoreInfo storeInfo;
    Esp32BaseAppEventLog::readStoreInfo(storeInfo);
    const bool readOk = Esp32BaseAppEventLog::readStoreRecords(0, storeInfo.count, sendAppEventCsvRow, &state);
    if (!readOk) {
        ESP32BASE_LOG_W("app_events", "csv_export_incomplete error=%s", Esp32BaseAppEventLog::lastError());
        sendChunk("# error,");
        Esp32BaseWeb::writeCsvEscaped(Esp32BaseAppEventLog::lastError());
        sendChunk("\n");
    }
    endResponse();
}

} // namespace esp32base_web

#endif
