#include "WebInternal.h"

#if ESP32BASE_ENABLE_WEB && ESP32BASE_ENABLE_APP_EVENTS

namespace esp32base_web {
namespace {

enum AppEventTimeFilter : uint8_t {
    APP_EVENT_TIME_ALL,
    APP_EVENT_TIME_REAL,
    APP_EVENT_TIME_UPTIME
};

struct AppEventFilter {
    uint16_t level = 0;
    AppEventTimeFilter time = APP_EVENT_TIME_ALL;
    uint32_t eventCode = 0;
    uint32_t reasonCode = 0;
    bool hasEventCode = false;
    bool hasReasonCode = false;
    char query[128] = "";
};

struct AppEventOutputState {
    const AppEventFilter* filter;
    uint32_t offset;
    uint32_t limit;
    uint32_t matched;
    uint32_t emitted;
    bool first;
};

bool parseUnsigned(const String& value, uint32_t minimum, uint32_t maximum, uint32_t& out) {
    if (value.length() == 0) {
        return false;
    }
    char* end = nullptr;
    errno = 0;
    const unsigned long parsed = strtoul(value.c_str(), &end, 10);
    if (errno != 0 || !end || *end != '\0' || parsed < minimum || parsed > maximum) {
        return false;
    }
    out = static_cast<uint32_t>(parsed);
    return true;
}

uint32_t readBoundedUnsignedArg(const char* name, uint32_t fallback, uint32_t minimum, uint32_t maximum) {
    if (!g_server.hasArg(name)) {
        return fallback;
    }
    uint32_t value = fallback;
    return parseUnsigned(g_server.arg(name), minimum, maximum, value) ? value : fallback;
}

void appendFilterQuery(char* query, size_t length, const char* name, const char* value) {
    if (!query || length == 0 || !name || !value || !value[0]) {
        return;
    }
    const size_t used = strlen(query);
    snprintf(query + used, length - used, "%s%s=%s", used ? "&" : "", name, value);
}

bool readAppEventFilter(AppEventFilter& filter) {
    if (g_server.hasArg("level")) {
        const String level = g_server.arg("level");
        if (level == "info") filter.level = static_cast<uint16_t>(Esp32BaseAppEvents::Level::Info);
        else if (level == "warning") filter.level = static_cast<uint16_t>(Esp32BaseAppEvents::Level::Warning);
        else if (level == "error") filter.level = static_cast<uint16_t>(Esp32BaseAppEvents::Level::Error);
        else if (level.length() != 0) return false;
        appendFilterQuery(filter.query, sizeof(filter.query), "level", level.c_str());
    }
    if (g_server.hasArg("time")) {
        const String time = g_server.arg("time");
        if (time == "real") filter.time = APP_EVENT_TIME_REAL;
        else if (time == "uptime") filter.time = APP_EVENT_TIME_UPTIME;
        else if (time.length() != 0) return false;
        appendFilterQuery(filter.query, sizeof(filter.query), "time", time.c_str());
    }
    if (g_server.hasArg("eventCode") && g_server.arg("eventCode").length() > 0) {
        if (!parseUnsigned(g_server.arg("eventCode"), 1, UINT32_MAX, filter.eventCode)) return false;
        filter.hasEventCode = true;
        appendFilterQuery(filter.query, sizeof(filter.query), "eventCode", g_server.arg("eventCode").c_str());
    }
    if (g_server.hasArg("reasonCode") && g_server.arg("reasonCode").length() > 0) {
        if (!parseUnsigned(g_server.arg("reasonCode"), 0, UINT32_MAX, filter.reasonCode)) return false;
        filter.hasReasonCode = true;
        appendFilterQuery(filter.query, sizeof(filter.query), "reasonCode", g_server.arg("reasonCode").c_str());
    }
    return true;
}

uint32_t resolvedCompletedEpoch(const Esp32BaseAppEvents::EventRecord& event) {
    uint32_t epoch = 0;
    Esp32BaseRecordStore::resolveCompletedEpoch(event.timing, epoch);
    return epoch;
}

uint32_t resolvedStartedEpoch(const Esp32BaseAppEvents::EventRecord& event) {
    uint32_t epoch = 0;
    Esp32BaseRecordStore::resolveStartedEpoch(event.timing, epoch);
    return epoch;
}

bool appEventMatches(const Esp32BaseAppEvents::EventRecord& event, const AppEventFilter& filter) {
    if (filter.level != 0 && static_cast<uint16_t>(event.level) != filter.level) return false;
    const bool realTime = resolvedCompletedEpoch(event) != 0;
    if (filter.time == APP_EVENT_TIME_REAL && !realTime) return false;
    if (filter.time == APP_EVENT_TIME_UPTIME && realTime) return false;
    if (filter.hasEventCode && event.eventCode != filter.eventCode) return false;
    if (filter.hasReasonCode && event.reasonCode != filter.reasonCode) return false;
    return true;
}

Esp32BaseWeb::UiTone appEventTone(Esp32BaseAppEvents::Level level) {
    if (level == Esp32BaseAppEvents::Level::Error) return Esp32BaseWeb::UI_DANGER;
    if (level == Esp32BaseAppEvents::Level::Warning) return Esp32BaseWeb::UI_WARN;
    return Esp32BaseWeb::UI_INFO;
}

void formatAppEventTime(const Esp32BaseAppEvents::EventRecord& event, char* out, size_t length) {
    const uint32_t epoch = resolvedCompletedEpoch(event);
    if (epoch != 0 && Esp32BaseTime::formatEpoch(epoch, out, length, "%Y-%m-%d %H:%M:%S")) {
        return;
    }
    snprintf(out, length, "boot %lu uptime %lu s",
             static_cast<unsigned long>(event.timing.completedBootId),
             static_cast<unsigned long>(event.timing.completedUptimeSec));
}

void sendEventJson(const Esp32BaseAppEvents::EventRecord& event) {
    sendChunk("{\"recordId\":"); sendUintChunk(event.recordId);
    sendChunk(",\"completedEpochSec\":"); sendUintChunk(event.timing.completedEpochSec);
    sendChunk(",\"resolvedCompletedEpochSec\":"); sendUintChunk(resolvedCompletedEpoch(event));
    sendChunk(",\"resolvedStartedEpochSec\":"); sendUintChunk(resolvedStartedEpoch(event));
    sendChunk(",\"completedBootId\":"); sendUintChunk(event.timing.completedBootId);
    sendChunk(",\"completedUptimeSec\":"); sendUintChunk(event.timing.completedUptimeSec);
    sendChunk(",\"durationSec\":"); sendUintChunk(event.timing.durationSec);
    sendChunk(",\"level\":\""); sendEscapedJsonChunk(Esp32BaseAppEvents::levelName(event.level));
    sendChunk("\",\"eventKind\":\""); sendEscapedJsonChunk(Esp32BaseAppEvents::eventKindName(event.eventKind));
    sendChunk("\",\"conditionId\":"); sendUintChunk(event.conditionId);
    sendChunk(",\"eventCode\":"); sendUintChunk(event.eventCode);
    sendChunk(",\"reasonCode\":"); sendUintChunk(event.reasonCode);
    sendChunk(",\"objectId\":"); sendUintChunk(event.objectId);
    sendChunk(",\"value1\":"); sendIntChunk(event.value1);
    sendChunk(",\"value2\":"); sendIntChunk(event.value2);
    sendChunk(",\"flags\":"); sendUintChunk(event.flags);
    sendChunk("}");
}

void sendDetailRowText(const char* label, const char* value, const char* help = nullptr) {
    sendChunk("<div><b>");
    sendEscapedHtmlChunk(label);
    sendChunk("</b><code>");
    sendEscapedHtmlChunk(value && value[0] ? value : "-");
    sendChunk("</code>");
    if (help && help[0]) {
        sendChunk("<small>");
        sendEscapedHtmlChunk(help);
        sendChunk("</small>");
    }
    sendChunk("</div>");
}

void sendDetailRowUint(const char* label, uint32_t value, const char* help = nullptr) {
    char text[16];
    snprintf(text, sizeof(text), "%lu", static_cast<unsigned long>(value));
    sendDetailRowText(label, text, help);
}

void sendDetailRowInt(const char* label, int32_t value, const char* help = nullptr) {
    char text[16];
    snprintf(text, sizeof(text), "%ld", static_cast<long>(value));
    sendDetailRowText(label, text, help);
}

void sendDetailGroupStart(const char* title, const char* help) {
    sendChunk("<section class='appevdetailgroup'><h3>");
    sendEscapedHtmlChunk(title);
    sendChunk("</h3>");
    if (help && help[0]) {
        sendChunk("<p>");
        sendEscapedHtmlChunk(help);
        sendChunk("</p>");
    }
    sendChunk("<div class='appevdetailgrid'>");
}

void sendDetailGroupEnd() {
    sendChunk("</div></section>");
}

void formatStartedTime(const Esp32BaseAppEvents::EventRecord& event, char* out, size_t length) {
    const uint32_t epoch = resolvedStartedEpoch(event);
    if (epoch != 0 && Esp32BaseTime::formatEpoch(epoch, out, length, "%Y-%m-%d %H:%M:%S")) return;
    if (event.timing.durationSec <= event.timing.completedUptimeSec) {
        snprintf(out, length, "boot %lu uptime %lu s",
                 static_cast<unsigned long>(event.timing.completedBootId),
                 static_cast<unsigned long>(event.timing.completedUptimeSec - event.timing.durationSec));
        return;
    }
    snprintf(out, length, "unavailable");
}

void sendAppEventDetailDialog(const Esp32BaseAppEvents::EventRecord& event, const char* dialogId) {
    char completed[48];
    char started[48];
    char flagsHex[12];
    formatAppEventTime(event, completed, sizeof(completed));
    formatStartedTime(event, started, sizeof(started));
    snprintf(flagsHex, sizeof(flagsHex), "0x%02x", static_cast<unsigned>(event.flags));

    sendChunk("<dialog id='");
    sendEscapedHtmlChunk(dialogId);
    sendChunk("' class='panel eb-modal appevdialog' data-eb-light-dismiss='1'><h2>App Event #");
    sendUintChunk(event.recordId);
    sendChunk("</h2>");

    sendDetailGroupStart("Identity & level", "Record identity and the generic event severity stored by Esp32Base.");
    sendDetailRowUint("recordId", event.recordId, "Monotonic ID within this App Events store version.");
    sendDetailRowText("levelName", Esp32BaseAppEvents::levelName(event.level));
    sendDetailRowUint("level", static_cast<uint8_t>(event.level));
    sendDetailRowText("eventKind", Esp32BaseAppEvents::eventKindName(event.eventKind));
    sendDetailRowUint("conditionId", event.conditionId, "0 identifies a discrete event; 1..32 identify condition transitions.");
    sendDetailGroupEnd();

    sendDetailGroupStart("Timing", "Completion metadata supplied by Record Store. Started time is derived from completion and duration.");
    sendDetailRowText("completed", completed);
    sendDetailRowText("started", started);
    sendDetailRowUint("completedEpochSec", event.timing.completedEpochSec, "0 means no trusted epoch was available when written.");
    sendDetailRowUint("resolvedCompletedEpochSec", resolvedCompletedEpoch(event));
    sendDetailRowUint("resolvedStartedEpochSec", resolvedStartedEpoch(event));
    sendDetailRowUint("completedBootId", event.timing.completedBootId);
    sendDetailRowUint("completedUptimeSec", event.timing.completedUptimeSec);
    sendDetailRowUint("durationSec", event.timing.durationSec);
    sendDetailGroupEnd();

    sendDetailGroupStart("Event fields", "Application-defined numeric fields. Esp32Base stores them without interpreting business meaning.");
    sendDetailRowUint("eventCode", event.eventCode);
    sendDetailRowUint("reasonCode", event.reasonCode);
    sendDetailRowUint("objectId", event.objectId);
    sendDetailGroupEnd();

    sendDetailGroupStart("Values & flags", "Application-defined numeric values and bit flags.");
    sendDetailRowInt("value1", event.value1);
    sendDetailRowInt("value2", event.value2);
    sendDetailRowUint("flags", event.flags);
    sendDetailRowText("flagsHex", flagsHex);
    sendDetailGroupEnd();

    sendChunk("<form method='dialog' class='actions'><button class='secondary'>Close</button></form></dialog>");
}

void sendHtmlEvent(const Esp32BaseAppEvents::EventRecord& event, void* user) {
    AppEventOutputState* state = static_cast<AppEventOutputState*>(user);
    if (!state || !appEventMatches(event, *state->filter)) return;
    const uint32_t index = state->matched++;
    if (index < state->offset || state->emitted >= state->limit) return;
    ++state->emitted;
    char time[48];
    formatAppEventTime(event, time, sizeof(time));
    sendChunk("<tr><td class='appevid'>"); sendUintChunk(event.recordId);
    sendChunk("</td><td class='appevtime'><span>"); sendEscapedHtmlChunk(time);
    sendChunk("</span><small>boot "); sendUintChunk(event.timing.completedBootId);
    sendChunk(" / uptime "); sendUintChunk(event.timing.completedUptimeSec);
    sendChunk(" s / duration "); sendUintChunk(event.timing.durationSec); sendChunk(" s</small></td><td>");
    sendStatusTag(appEventTone(event.level), Esp32BaseAppEvents::levelName(event.level));
    sendChunk("</td><td><span class='appevmain'>Event "); sendUintChunk(event.eventCode);
    sendChunk("</span><small class='appevsub'>Reason "); sendUintChunk(event.reasonCode);
    sendChunk(" / "); sendEscapedHtmlChunk(Esp32BaseAppEvents::eventKindName(event.eventKind));
    if (event.conditionId != 0) { sendChunk(" #"); sendUintChunk(event.conditionId); }
    sendChunk("</small></td><td class='appevobject'>"); sendUintChunk(event.objectId);
    sendChunk("</td><td class='appevvalues'><div class='appevchips'><span class='appevchip'>v1 ");
    sendIntChunk(event.value1);
    sendChunk("</span><span class='appevchip'>v2 "); sendIntChunk(event.value2);
    sendChunk("</span><span class='appevchip'>flags "); sendUintChunk(event.flags);
    sendChunk("</span></div></td><td>");
    char dialogId[24];
    snprintf(dialogId, sizeof(dialogId), "appev-%lu", static_cast<unsigned long>(event.recordId));
    sendChunk("<button type='button' class='btnlink compact' onclick=\"document.getElementById('");
    sendEscapedHtmlChunk(dialogId);
    sendChunk("').showModal()\">Details</button>");
    sendAppEventDetailDialog(event, dialogId);
    sendChunk("</td></tr>");
}

void sendJsonEvent(const Esp32BaseAppEvents::EventRecord& event, void* user) {
    AppEventOutputState* state = static_cast<AppEventOutputState*>(user);
    if (!state || !appEventMatches(event, *state->filter)) return;
    const uint32_t index = state->matched++;
    if (index < state->offset || state->emitted >= state->limit) return;
    if (!state->first) sendChunk(",");
    state->first = false;
    ++state->emitted;
    sendEventJson(event);
}

void sendCsvEvent(const Esp32BaseAppEvents::EventRecord& event, void* user) {
    AppEventOutputState* state = static_cast<AppEventOutputState*>(user);
    if (!state || !appEventMatches(event, *state->filter)) return;
    ++state->matched;
    sendUintChunk(event.recordId); sendChunk(",");
    sendUintChunk(event.timing.completedEpochSec); sendChunk(",");
    sendUintChunk(resolvedCompletedEpoch(event)); sendChunk(",");
    sendUintChunk(resolvedStartedEpoch(event)); sendChunk(",");
    sendUintChunk(event.timing.completedBootId); sendChunk(",");
    sendUintChunk(event.timing.completedUptimeSec); sendChunk(",");
    sendUintChunk(event.timing.durationSec); sendChunk(",");
    Esp32BaseWeb::writeCsvEscaped(Esp32BaseAppEvents::levelName(event.level)); sendChunk(",");
    Esp32BaseWeb::writeCsvEscaped(Esp32BaseAppEvents::eventKindName(event.eventKind)); sendChunk(",");
    sendUintChunk(event.conditionId); sendChunk(",");
    sendUintChunk(event.eventCode); sendChunk(",");
    sendUintChunk(event.reasonCode); sendChunk(",");
    sendUintChunk(event.objectId); sendChunk(",");
    sendIntChunk(event.value1); sendChunk(",");
    sendIntChunk(event.value2); sendChunk(",");
    sendUintChunk(event.flags); sendChunk("\n");
}

void sendInvalidFilter(bool json) {
    if (!beginResponse(400, json ? "application/json" : "text/plain", nullptr)) return;
    sendChunk(json ? "{\"ok\":false,\"error\":\"invalid_filter\"}" : "invalid_filter\n");
    endResponse();
}

void sendFilterPanel(const AppEventFilter& filter, uint32_t per) {
    sendChunk("<section class='panel formpanel appevfilters'><h2>Filter &amp; Export</h2><form method='get' action='/esp32base/app-events' class='editform'>");
    sendChunk("<input type='hidden' name='per' value='"); sendUintChunk(per); sendChunk("'><div class='fieldgrid appevfiltergrid'>");
    sendChunk("<div class='field'><label for='appev-level'>Level</label><select id='appev-level' name='level'><option value=''>All</option>");
    const char* levels[] = {"info", "warning", "error"};
    for (size_t i = 0; i < 3; ++i) {
        sendChunk("<option value='"); sendChunk(levels[i]); sendChunk("'");
        if ((filter.level == 1 && i == 0) || (filter.level == 2 && i == 1) || (filter.level == 3 && i == 2)) sendChunk(" selected");
        sendChunk(">"); sendChunk(levels[i]); sendChunk("</option>");
    }
    sendChunk("</select></div><div class='field'><label for='appev-time'>Time</label><select id='appev-time' name='time'><option value=''>All</option><option value='real'");
    if (filter.time == APP_EVENT_TIME_REAL) sendChunk(" selected");
    sendChunk(">Real time</option><option value='uptime'");
    if (filter.time == APP_EVENT_TIME_UPTIME) sendChunk(" selected");
    sendChunk(">Uptime</option></select></div><div class='field'><label for='appev-event-code'>Event code</label><input id='appev-event-code' name='eventCode' inputmode='numeric' value='");
    if (filter.hasEventCode) sendUintChunk(filter.eventCode);
    sendChunk("'></div><div class='field'><label for='appev-reason-code'>Reason code</label><input id='appev-reason-code' name='reasonCode' inputmode='numeric' value='");
    if (filter.hasReasonCode) sendUintChunk(filter.reasonCode);
    sendChunk("'></div></div><div class='actions appevactions'><input type='submit' value='Apply'><a class='btnlink secondary' href='/esp32base/app-events'>Reset</a><a class='btnlink' href='/esp32base/app-events.csv");
    if (filter.query[0]) { sendChunk("?"); sendEscapedHtmlChunk(filter.query); }
    sendChunk("'>Export CSV</a></div></form></section>");
}

void sendStoreSummary(const Esp32BaseAppEvents::AppEventsStatus& status) {
    char value[64];
    char capacity[24];
    char damaged[24];
    char currentSize[24];
    char maximumSize[24];
    char storeSize[64];
    snprintf(value, sizeof(value), "%lu", static_cast<unsigned long>(status.eventStore.recordCount));
    snprintf(capacity, sizeof(capacity), "%lu", static_cast<unsigned long>(status.eventStore.capacity));
    snprintf(damaged, sizeof(damaged), "%lu", static_cast<unsigned long>(status.eventStore.damagedRecordCount));
    formatReadableBytes(status.eventStore.currentStoreBytes, currentSize, sizeof(currentSize));
    formatReadableBytes(status.eventStore.maximumStoreBytes, maximumSize, sizeof(maximumSize));
    snprintf(storeSize, sizeof(storeSize), "%s / %s", currentSize, maximumSize);
    Esp32BaseWeb::beginMetricGrid();
    Esp32BaseWeb::sendMetric("Records", value, "Valid records currently retained.");
    Esp32BaseWeb::sendMetric("Capacity", capacity, "Estimated peak capacity before segment rotation.");
    Esp32BaseWeb::sendMetric("Store", storeSize, "Current logical size and configured budget.");
    Esp32BaseWeb::sendMetric("Damaged", damaged, "Corrupt or incomplete records skipped during reads.");
#if ESP32BASE_ENABLE_APP_EVENT_CONDITIONS
    snprintf(value, sizeof(value), "%u", static_cast<unsigned>(status.activeConditionCount));
    Esp32BaseWeb::sendMetric("Active conditions", value, "Confirmed condition IDs restored from NVS.");
#endif
    Esp32BaseWeb::endMetricGrid();

    sendChunk("<section class='panel appestore'><h2>Storage</h2><div class='tablewrap'><table class='kv'>");
    sendTaggedInfoRow("State", Esp32BaseRecordStore::storeStateName(status.eventStore.state),
                      status.eventStore.state == Esp32BaseRecordStore::StoreState::Ready ? Esp32BaseWeb::UI_OK :
                      (status.eventStore.state == Esp32BaseRecordStore::StoreState::Degraded ? Esp32BaseWeb::UI_WARN : Esp32BaseWeb::UI_DANGER));
    sendInfoRow("Path", status.eventStore.path ? status.eventStore.path : "-");
    snprintf(value, sizeof(value), "%lu", static_cast<unsigned long>(status.eventStore.segmentCount));
    sendInfoRow("Segments", value);
    formatReadableBytes(status.eventStore.segmentFileLimitBytes, value, sizeof(value));
    sendInfoRow("Segment limit", value);
    snprintf(value, sizeof(value), "%lu B", static_cast<unsigned long>(status.eventStore.slotSizeBytes));
    sendInfoRow("Record size", value);
    snprintf(value, sizeof(value), "%lu / %lu / %lu",
             static_cast<unsigned long>(status.eventStore.oldestRecordId),
             static_cast<unsigned long>(status.eventStore.newestRecordId),
             static_cast<unsigned long>(status.eventStore.nextRecordId));
    sendInfoRow("Oldest / newest / next ID", value);
    sendInfoRow("Last event store error", status.eventStore.errorReason ? status.eventStore.errorReason : "none");
#if ESP32BASE_ENABLE_APP_EVENT_CONDITIONS
    sendTaggedInfoRow("Condition state", status.conditionStateLoaded ? "loaded" : "unavailable",
                      status.conditionStateLoaded ? Esp32BaseWeb::UI_OK : Esp32BaseWeb::UI_DANGER);
    sendInfoRow("Condition state save pending", status.conditionStateSavePending ? "yes" : "no");
#endif
    sendChunk("</table></div></section>");
}

} // namespace

void handleAppEventsPage() {
    markRequest();
    if (!ensureAuth()) return;
    AppEventFilter filter;
    if (!readAppEventFilter(filter)) { sendInvalidFilter(false); return; }
    const uint32_t per = readBoundedUnsignedArg("per", 10, 1, 100);
    const uint32_t page = readBoundedUnsignedArg("page", 1, 1, 65535);
    Esp32BaseAppEvents::AppEventsStatus status;
    Esp32BaseAppEvents::readStatus(status);

    Esp32BaseWeb::sendHeader(g_builtinLabels[Esp32BaseWeb::BUILTIN_APP_EVENTS]);
    Esp32BaseWeb::sendPageTitle(g_builtinLabels[Esp32BaseWeb::BUILTIN_APP_EVENTS], "Numeric application events stored by Esp32Base.");
    if (!status.eventStore.ready) Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_DANGER, "App Events unavailable", status.eventStore.errorReason);
    else if (status.eventStore.state == Esp32BaseRecordStore::StoreState::Degraded) Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_WARN, "App Events degraded", "Damaged records are skipped.");
#if ESP32BASE_ENABLE_APP_EVENT_CONDITIONS
    if (!status.conditionStateLoaded || status.conditionStateSavePending) Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_DANGER, "Condition tracking persistence unavailable", Esp32BaseAppEvents::lastErrorReason());
#endif
    sendStoreSummary(status);
    sendFilterPanel(filter, per);
    sendChunk("<section class='panel'><h2>Events</h2><div class='tablewrap'><table class='appevtable'><tr><th>ID</th><th>Completed</th><th>Level</th><th>Event</th><th>Object ID</th><th>Values</th><th>Action</th></tr>");
    AppEventOutputState output = {&filter, (page - 1U) * per, per, 0, 0, true};
    const bool readOk = Esp32BaseAppEvents::readLatest(0, status.eventStore.recordCount, sendHtmlEvent, &output);
    if (!readOk) sendChunk("<tr><td colspan='7'>Read failed</td></tr>");
    else if (output.emitted == 0) sendChunk("<tr><td colspan='7'>No App Events</td></tr>");
    sendChunk("</table></div>");
    Esp32BaseWeb::Pagination pagination = {"/esp32base/app-events", filter.query, page, per, output.matched};
    Esp32BaseWeb::sendPagination(pagination);
    sendChunk("</section>");
    Esp32BaseWeb::sendFooter();
}

void handleAppEventsApi() {
    markRequest();
    if (!ensureAuth()) return;
    AppEventFilter filter;
    if (!readAppEventFilter(filter)) { sendInvalidFilter(true); return; }
    const uint32_t offset = readBoundedUnsignedArg("offset", 0, 0, UINT32_MAX);
    const uint32_t limit = readBoundedUnsignedArg("limit", 50, 1, 100);
    Esp32BaseAppEvents::AppEventsStatus status;
    Esp32BaseAppEvents::readStatus(status);
    if (!beginResponse(200, "application/json", nullptr)) return;
    bool appEventsOk = status.eventStore.ready;
#if ESP32BASE_ENABLE_APP_EVENT_CONDITIONS
    appEventsOk = appEventsOk && status.conditionStateLoaded && !status.conditionStateSavePending;
#endif
    sendChunk("{\"kind\":\"app_events\",\"ok\":"); sendChunk(appEventsOk ? "true" : "false");
    sendChunk(",\"state\":\""); sendEscapedJsonChunk(Esp32BaseRecordStore::storeStateName(status.eventStore.state));
    sendChunk("\",\"error\":\""); sendEscapedJsonChunk(status.eventStore.errorReason);
    sendChunk("\",\"count\":"); sendUintChunk(status.eventStore.recordCount);
    sendChunk(",\"capacity\":"); sendUintChunk(status.eventStore.capacity);
    sendChunk(",\"damagedRecordCount\":"); sendUintChunk(status.eventStore.damagedRecordCount);
#if ESP32BASE_ENABLE_APP_EVENT_CONDITIONS
    sendChunk(",\"conditionStateLoaded\":"); sendChunk(status.conditionStateLoaded ? "true" : "false");
    sendChunk(",\"conditionStateSavePending\":"); sendChunk(status.conditionStateSavePending ? "true" : "false");
    sendChunk(",\"activeConditionCount\":"); sendUintChunk(status.activeConditionCount);
    sendChunk(",\"conditionStateError\":\"");
    if (!status.conditionStateLoaded || status.conditionStateSavePending) {
        sendEscapedJsonChunk(Esp32BaseAppEvents::lastErrorReason());
    }
    sendChunk("\"");
#endif
    sendChunk(",\"offset\":"); sendUintChunk(offset);
    sendChunk(",\"limit\":"); sendUintChunk(limit);
    sendChunk(",\"events\":[");
    AppEventOutputState output = {&filter, offset, limit, 0, 0, true};
    const bool readOk = Esp32BaseAppEvents::readLatest(0, status.eventStore.recordCount, sendJsonEvent, &output);
    sendChunk("],\"matched\":"); sendUintChunk(output.matched);
    sendChunk(",\"returned\":"); sendUintChunk(output.emitted);
    sendChunk(",\"readOk\":"); sendChunk(readOk ? "true" : "false"); sendChunk("}");
    endResponse();
}

void handleAppEventsCsv() {
    markRequest();
    if (!ensureAuth()) return;
    AppEventFilter filter;
    if (!readAppEventFilter(filter)) { sendInvalidFilter(false); return; }
    Esp32BaseAppEvents::AppEventsStatus status;
    Esp32BaseAppEvents::readStatus(status);
    if (!Esp32BaseWeb::beginCsv(200, "app-events.csv")) return;
    sendChunk("record_id,completed_epoch_sec,resolved_completed_epoch_sec,resolved_started_epoch_sec,completed_boot_id,completed_uptime_sec,duration_sec,level,event_kind,condition_id,event_code,reason_code,object_id,value1,value2,flags\n");
    AppEventOutputState output = {&filter, 0, UINT32_MAX, 0, 0, true};
    const bool readOk = Esp32BaseAppEvents::readLatest(0, status.eventStore.recordCount, sendCsvEvent, &output);
    if (!readOk) sendChunk("# error,read_failed\n");
    endResponse();
}

} // namespace esp32base_web

#endif
