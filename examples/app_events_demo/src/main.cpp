#include <Arduino.h>
#include <Esp32Base.h>

namespace {
uint32_t g_manualEventId = 0;

bool appendDemoEvent(Esp32BaseAppEventLog::Level level,
                     const char* source,
                     const char* type,
                     const char* reason,
                     const char* object,
                     uint16_t code,
                     int32_t value1,
                     int32_t value2,
                     int32_t value3,
                     uint8_t valueMask,
                     const char* text) {
    Esp32BaseAppEventLog::Event event;
    event.level = level;
    event.source = source;
    event.type = type;
    event.reason = reason;
    event.object = object;
    event.code = code;
    event.value1 = value1;
    event.value2 = value2;
    event.value3 = value3;
    event.valueMask = valueMask;
    event.text = text;
    const bool ok = Esp32BaseAppEventLog::append(event);
    if (!ok) {
        ESP32BASE_LOG_W("demo", "app_event_append_failed error=%s", Esp32BaseAppEventLog::lastError());
    }
    return ok;
}

void seedDemoEvents() {
    appendDemoEvent(Esp32BaseAppEventLog::LEVEL_INFO,
                    "config",
                    "state_ready",
                    "sample_config_loaded",
                    "config:demo",
                    1,
                    3,
                    0,
                    0,
                    Esp32BaseAppEventLog::VALUE1,
                    "sample config ready");

    appendDemoEvent(Esp32BaseAppEventLog::LEVEL_INFO,
                    "scheduler",
                    "job_skipped",
                    "manual",
                    "job:alpha",
                    100,
                    15,
                    0,
                    0,
                    Esp32BaseAppEventLog::VALUE1,
                    "sample schedule decision");

    appendDemoEvent(Esp32BaseAppEventLog::LEVEL_INFO,
                    "api",
                    "external_decision",
                    "accepted",
                    "decision:42",
                    200,
                    7,
                    2,
                    0,
                    Esp32BaseAppEventLog::VALUE1 | Esp32BaseAppEventLog::VALUE2,
                    "external system accepted");

    appendDemoEvent(Esp32BaseAppEventLog::LEVEL_WARN,
                    "monitor",
                    "anomaly_detected",
                    "missing_signal",
                    "sensor:meter-a",
                    300,
                    0,
                    30,
                    1,
                    Esp32BaseAppEventLog::VALUE1 | Esp32BaseAppEventLog::VALUE2 | Esp32BaseAppEventLog::VALUE3,
                    "sample warning event");
}

void handleDemoEventsPage() {
    if (!Esp32BaseWeb::checkAuth()) {
        return;
    }
    Esp32BaseWeb::sendHeader("Demo Events");
    Esp32BaseWeb::sendPageTitle("Demo Events");
    if (Esp32BaseWeb::hasParam("posted")) {
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_OK, "Demo event written");
    } else if (Esp32BaseWeb::hasParam("error")) {
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_DANGER, "Demo event failed", Esp32BaseAppEventLog::lastError());
    }
    Esp32BaseWeb::beginPanel("Manual Event");
    Esp32BaseWeb::sendChunk("<form method='post' action='/demo/events/add' onsubmit=\"return once(this)\"><div class='actions'><input type='submit' value='Write Demo Event'></div></form>");
    Esp32BaseWeb::sendChunk("<p><a href='/esp32base/app-events'>Open App Events</a></p>");
    Esp32BaseWeb::endPanel();
    Esp32BaseWeb::sendFooter();
}

void handleDemoEventAdd() {
    if (!Esp32BaseWeb::checkPostAllowed("demo_event_add")) {
        return;
    }
    ++g_manualEventId;
    const bool ok = appendDemoEvent(Esp32BaseAppEventLog::LEVEL_INFO,
                                    "demo",
                                    "manual_event",
                                    "web_post",
                                    "demo:manual",
                                    10,
                                    static_cast<int32_t>(g_manualEventId),
                                    static_cast<int32_t>(millis() / 1000UL),
                                    0,
                                    Esp32BaseAppEventLog::VALUE1 | Esp32BaseAppEventLog::VALUE2,
                                    "posted from demo route");
    Esp32BaseWeb::redirectSeeOther(ok ? "/demo/events?posted=1" : "/demo/events?error=1");
}
} // namespace

void setup() {
    Esp32Base::setFirmwareInfo("app-events-demo", "1.0.0");
    Esp32BaseWeb::setDefaultAuth("admin", "admin");
    Esp32BaseWeb::addNavItem("/demo/events", "Demo Events");
    Esp32BaseWeb::addPage("/demo/events", "Demo Events", handleDemoEventsPage);
    Esp32BaseWeb::addRoute("/demo/events/add", Esp32BaseWeb::METHOD_POST, handleDemoEventAdd);

    Esp32Base::begin();
    seedDemoEvents();
    ESP32BASE_LOG_I("example", "open /esp32base/app-events or /demo/events");
}

void loop() {
    Esp32Base::handle();
    delay(10);
}
