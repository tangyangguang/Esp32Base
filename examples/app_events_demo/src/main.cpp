#include <Arduino.h>
#include <Esp32Base.h>

namespace {
uint32_t g_manualEventId = 0;
#if ESP32BASE_ENABLE_APP_EVENT_CONDITIONS
Esp32BaseAppEvents::ConditionStateTracker g_demoSensorUnavailableCondition(1, 5000, 3000);
uint32_t g_nextConditionObservationMs = 0;
#endif

bool appendDemoEvent(Esp32BaseAppEvents::Level level,
                     uint32_t eventCode,
                     uint32_t reasonCode,
                     uint32_t objectId,
                     int32_t value1,
                     int32_t value2,
                     uint8_t flags) {
    Esp32BaseAppEvents::EventInput event;
    event.level = level;
    event.eventCode = eventCode;
    event.reasonCode = reasonCode;
    event.objectId = objectId;
    event.value1 = value1;
    event.value2 = value2;
    event.flags = flags;
    const bool ok = Esp32BaseAppEvents::appendDiscreteEvent(event) ==
                    Esp32BaseAppEvents::DiscreteEventAppendResult::Stored;
    if (!ok) {
        ESP32BASE_LOG_W("demo", "app_event_append_failed error=%s", Esp32BaseAppEvents::lastErrorReason());
    }
    return ok;
}

void seedDemoEvents() {
    appendDemoEvent(Esp32BaseAppEvents::Level::Info, 1001, 0, 1, 30, 820, 0);
    appendDemoEvent(Esp32BaseAppEvents::Level::Warning, 1002, 2101, 1, 1450, 2, 0x0001);
    appendDemoEvent(Esp32BaseAppEvents::Level::Warning, 2001, 2201, 2, 0, 0, 0);
    appendDemoEvent(Esp32BaseAppEvents::Level::Warning, 3001, 2301, 3, 12, 20, 0x0002);
}

#if ESP32BASE_ENABLE_APP_EVENT_CONDITIONS
void observeDemoCondition() {
    const uint32_t nowMs = millis();
    if (static_cast<int32_t>(nowMs - g_nextConditionObservationMs) < 0) return;
    g_nextConditionObservationMs = nowMs + 250;

    // Synthetic demo: unavailable for the first 15 seconds, then recovered.
    // The application controls this polling schedule; confirmation durations do not poll hardware.
    const bool sensorUnavailable = nowMs < 15000;
    Esp32BaseAppEvents::EventInput event;
    event.level = sensorUnavailable ? Esp32BaseAppEvents::Level::Warning
                                    : Esp32BaseAppEvents::Level::Info;
    event.eventCode = sensorUnavailable ? 7001 : 7002;
    event.reasonCode = sensorUnavailable ? 2701 : 0;
    event.objectId = 1;
    const Esp32BaseAppEvents::ConditionObservationResult result =
        Esp32BaseAppEvents::observeConditionState(
            g_demoSensorUnavailableCondition,
            sensorUnavailable ? Esp32BaseAppEvents::ObservedConditionState::Active
                              : Esp32BaseAppEvents::ObservedConditionState::Inactive,
            event);
    if (result == Esp32BaseAppEvents::ConditionObservationResult::EventStoreWriteFailed ||
        result == Esp32BaseAppEvents::ConditionObservationResult::EventStoredButConditionStateSaveFailed) {
        ESP32BASE_LOG_W("demo", "condition_observation_failed error=%s",
                        Esp32BaseAppEvents::lastErrorReason());
    }
}
#endif

void handleDemoEventsPage() {
    if (!Esp32BaseWeb::checkAuth()) {
        return;
    }
    Esp32BaseWeb::sendHeader("Demo Events");
    Esp32BaseWeb::sendPageTitle("Demo Events");
    if (Esp32BaseWeb::hasParam("posted")) {
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_OK, "Demo event written");
    } else if (Esp32BaseWeb::hasParam("error")) {
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_DANGER, "Demo event failed", Esp32BaseAppEvents::lastErrorReason());
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
    const bool ok = appendDemoEvent(Esp32BaseAppEvents::Level::Info,
                                    9001,
                                    0,
                                    1,
                                    static_cast<int32_t>(g_manualEventId),
                                    static_cast<int32_t>(millis() / 1000UL),
                                    0);
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
#if ESP32BASE_ENABLE_APP_EVENT_CONDITIONS
    observeDemoCondition();
#endif
    delay(10);
}
