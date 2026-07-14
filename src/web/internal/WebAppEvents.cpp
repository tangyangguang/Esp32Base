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
    sendChunk("\",\"eventCode\":"); sendUintChunk(event.eventCode);
    sendChunk(",\"reasonCode\":"); sendUintChunk(event.reasonCode);
    sendChunk(",\"objectId\":"); sendUintChunk(event.objectId);
    sendChunk(",\"value1\":"); sendIntChunk(event.value1);
    sendChunk(",\"value2\":"); sendIntChunk(event.value2);
    sendChunk(",\"flags\":"); sendUintChunk(event.flags);
    sendChunk("}");
}

void sendHtmlEvent(const Esp32BaseAppEvents::EventRecord& event, void* user) {
    AppEventOutputState* state = static_cast<AppEventOutputState*>(user);
    if (!state || !appEventMatches(event, *state->filter)) return;
    const uint32_t index = state->matched++;
    if (index < state->offset || state->emitted >= state->limit) return;
    ++state->emitted;
    char time[48];
    formatAppEventTime(event, time, sizeof(time));
    sendChunk("<tr><td>"); sendUintChunk(event.recordId);
    sendChunk("</td><td>"); sendEscapedHtmlChunk(time);
    sendChunk("</td><td>"); sendUintChunk(event.timing.durationSec); sendChunk(" s</td><td>");
    sendStatusTag(appEventTone(event.level), Esp32BaseAppEvents::levelName(event.level));
    sendChunk("</td><td>"); sendUintChunk(event.eventCode);
    sendChunk("</td><td>"); sendUintChunk(event.reasonCode);
    sendChunk("</td><td>"); sendUintChunk(event.objectId);
    sendChunk("</td><td>"); sendIntChunk(event.value1);
    sendChunk("</td><td>"); sendIntChunk(event.value2);
    sendChunk("</td><td>"); sendUintChunk(event.flags);
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
    sendChunk("<section class='panel formpanel'><h2>Filter &amp; Export</h2><form method='get' action='/esp32base/app-events' class='editform'>");
    sendChunk("<input type='hidden' name='per' value='"); sendUintChunk(per); sendChunk("'><div class='fieldgrid'>");
    sendChunk("<label>Level<select name='level'><option value=''>All</option>");
    const char* levels[] = {"info", "warning", "error"};
    for (size_t i = 0; i < 3; ++i) {
        sendChunk("<option value='"); sendChunk(levels[i]); sendChunk("'");
        if ((filter.level == 1 && i == 0) || (filter.level == 2 && i == 1) || (filter.level == 3 && i == 2)) sendChunk(" selected");
        sendChunk(">"); sendChunk(levels[i]); sendChunk("</option>");
    }
    sendChunk("</select></label><label>Time<select name='time'><option value=''>All</option><option value='real'");
    if (filter.time == APP_EVENT_TIME_REAL) sendChunk(" selected");
    sendChunk(">Real time</option><option value='uptime'");
    if (filter.time == APP_EVENT_TIME_UPTIME) sendChunk(" selected");
    sendChunk(">Uptime</option></select></label><label>Event code<input name='eventCode' inputmode='numeric' value='");
    if (filter.hasEventCode) sendUintChunk(filter.eventCode);
    sendChunk("'></label><label>Reason code<input name='reasonCode' inputmode='numeric' value='");
    if (filter.hasReasonCode) sendUintChunk(filter.reasonCode);
    sendChunk("'></label></div><div class='actions'><input type='submit' value='Apply'><a class='btnlink secondary' href='/esp32base/app-events'>Reset</a><a class='btnlink' href='/esp32base/app-events.csv");
    if (filter.query[0]) { sendChunk("?"); sendEscapedHtmlChunk(filter.query); }
    sendChunk("'>Export CSV</a></div></form></section>");
}

void sendStoreSummary(const Esp32BaseAppEvents::EventStoreStatus& status) {
    char value[48];
    sendChunk("<section class='panel'><h2>Storage</h2><div class='infotable'>");
    sendTaggedInfoRow("State", Esp32BaseRecordStore::storeStateName(status.storage.state),
                      status.storage.state == Esp32BaseRecordStore::StoreState::Ready ? Esp32BaseWeb::UI_OK :
                      (status.storage.state == Esp32BaseRecordStore::StoreState::Degraded ? Esp32BaseWeb::UI_WARN : Esp32BaseWeb::UI_DANGER));
    snprintf(value, sizeof(value), "%lu / %lu", static_cast<unsigned long>(status.storage.recordCount), static_cast<unsigned long>(status.storage.capacity));
    sendInfoRow("Records", value);
    snprintf(value, sizeof(value), "%lu", static_cast<unsigned long>(status.storage.damagedRecordCount)); sendInfoRow("Damaged", value);
    formatReadableBytes(status.storage.currentStoreBytes, value, sizeof(value)); sendInfoRow("Store size", value);
    sendInfoRow("Path", status.storage.path ? status.storage.path : "-");
    sendInfoRow("Last error", status.storage.errorReason ? status.storage.errorReason : "none");
    sendChunk("</div></section>");
}

} // namespace

void handleAppEventsPage() {
    markRequest();
    if (!ensureAuth()) return;
    AppEventFilter filter;
    if (!readAppEventFilter(filter)) { sendInvalidFilter(false); return; }
    const uint32_t per = readBoundedUnsignedArg("per", 20, 1, 100);
    const uint32_t page = readBoundedUnsignedArg("page", 1, 1, 65535);
    Esp32BaseAppEvents::EventStoreStatus status;
    Esp32BaseAppEvents::readStatus(status);

    Esp32BaseWeb::sendHeader(g_builtinLabels[Esp32BaseWeb::BUILTIN_APP_EVENTS]);
    Esp32BaseWeb::sendPageTitle(g_builtinLabels[Esp32BaseWeb::BUILTIN_APP_EVENTS], "Compact application event records.");
    if (!status.storage.ready) Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_DANGER, "App Events unavailable", status.storage.errorReason);
    else if (status.storage.state == Esp32BaseRecordStore::StoreState::Degraded) Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_WARN, "App Events degraded", "Damaged records are skipped.");
    sendStoreSummary(status);
    sendFilterPanel(filter, per);
    sendChunk("<section class='panel'><h2>Events</h2><div class='tablewrap'><table><tr><th>ID</th><th>Completed</th><th>Duration</th><th>Level</th><th>Event code</th><th>Reason code</th><th>Object ID</th><th>Value 1</th><th>Value 2</th><th>Flags</th></tr>");
    AppEventOutputState output = {&filter, (page - 1U) * per, per, 0, 0, true};
    const bool readOk = Esp32BaseAppEvents::readLatest(0, status.storage.recordCount, sendHtmlEvent, &output);
    if (!readOk) sendChunk("<tr><td colspan='10'>Read failed</td></tr>");
    else if (output.emitted == 0) sendChunk("<tr><td colspan='10'>No App Events</td></tr>");
    sendChunk("</table></div></section>");
    Esp32BaseWeb::Pagination pagination = {"/esp32base/app-events", filter.query, page, per, output.matched};
    Esp32BaseWeb::sendPagination(pagination);
    Esp32BaseWeb::sendFooter();
}

void handleAppEventsApi() {
    markRequest();
    if (!ensureAuth()) return;
    AppEventFilter filter;
    if (!readAppEventFilter(filter)) { sendInvalidFilter(true); return; }
    const uint32_t offset = readBoundedUnsignedArg("offset", 0, 0, UINT32_MAX);
    const uint32_t limit = readBoundedUnsignedArg("limit", 50, 1, 100);
    Esp32BaseAppEvents::EventStoreStatus status;
    Esp32BaseAppEvents::readStatus(status);
    if (!beginResponse(200, "application/json", nullptr)) return;
    sendChunk("{\"kind\":\"app_events\",\"ok\":"); sendChunk(status.storage.ready ? "true" : "false");
    sendChunk(",\"state\":\""); sendEscapedJsonChunk(Esp32BaseRecordStore::storeStateName(status.storage.state));
    sendChunk("\",\"error\":\""); sendEscapedJsonChunk(status.storage.errorReason);
    sendChunk("\",\"count\":"); sendUintChunk(status.storage.recordCount);
    sendChunk(",\"capacity\":"); sendUintChunk(status.storage.capacity);
    sendChunk(",\"damagedRecordCount\":"); sendUintChunk(status.storage.damagedRecordCount);
    sendChunk(",\"offset\":"); sendUintChunk(offset);
    sendChunk(",\"limit\":"); sendUintChunk(limit);
    sendChunk(",\"events\":[");
    AppEventOutputState output = {&filter, offset, limit, 0, 0, true};
    const bool readOk = Esp32BaseAppEvents::readLatest(0, status.storage.recordCount, sendJsonEvent, &output);
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
    Esp32BaseAppEvents::EventStoreStatus status;
    Esp32BaseAppEvents::readStatus(status);
    if (!Esp32BaseWeb::beginCsv(200, "app-events.csv")) return;
    sendChunk("record_id,completed_epoch_sec,resolved_completed_epoch_sec,resolved_started_epoch_sec,completed_boot_id,completed_uptime_sec,duration_sec,level,event_code,reason_code,object_id,value1,value2,flags\n");
    AppEventOutputState output = {&filter, 0, UINT32_MAX, 0, 0, true};
    const bool readOk = Esp32BaseAppEvents::readLatest(0, status.storage.recordCount, sendCsvEvent, &output);
    if (!readOk) sendChunk("# error,read_failed\n");
    endResponse();
}

} // namespace esp32base_web

#endif
