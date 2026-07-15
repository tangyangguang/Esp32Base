# App Events Demo

This sample enables `ESP32BASE_ENABLE_APP_EVENTS=1` on `ESP32BASE_PROFILE_FULL`. App Events are application/business events; the System Logs page at `/esp32base/logs` remains the `Esp32BaseFileLog` system diagnostic log for boot, WiFi, OTA, FS and other base-library diagnostics.

It writes compact discrete door-opening, feeding and watering events during boot. It also observes a synthetic sensor-unavailable condition every 250 ms: activation must remain observed for 5 seconds and recovery for 3 seconds, demonstrating that confirmation durations do not schedule hardware polling. It then exposes:

- `/esp32base/app-events` for the built-in numeric App Events diagnostic page, including latest-first pagination and a complete per-event detail dialog
- `/esp32base/app-events?level=warning&time=real` for common UI filters
- `/esp32base/api/app-events?offset=0&limit=50&eventCode=1001` for filtered paged JSON
- `/esp32base/app-events.csv?reasonCode=2101` for filtered CSV export
- `/demo/events` for a small demo page with a POST button that writes one more event

Application code that needs a business event list or detail page should use `Esp32BaseAppEvents::readLatest()` or `/esp32base/api/app-events`, then map `eventCode`, `reasonCode` and `flags` to business language. Use `appendDiscreteEvent()` for independent actions and a long-lived `ConditionStateTracker` plus `observeConditionState()` for polled persistent conditions. A condition ID is a persistent schema identifier and must keep the same meaning across firmware updates that retain NVS. The sketch reacts to every actionable observation result but logs only when the result changes, so an unavailable store cannot create a second warning storm. The built-in page intentionally shows numeric fields and does not register business labels.

The Completed column shows wall-clock time when RTC/NTP or the current boot mapping can resolve it. Otherwise it shows uptime plus the boot id, so relative startup events are not confused with real dates.

The demo sketch calls `Esp32BaseWeb::setDefaultAuth("admin", "admin")` for local testing only. Business projects must set their own credentials before `Esp32Base::begin()`.

Build:

```sh
pio run -d examples/app_events_demo
pio run -d examples/app_events_demo -e esp32s3_full
pio run -d examples/app_events_demo -e esp32c3_full
pio run -d examples/app_events_demo -e esp32_arduino3
pio run -d examples/app_events_demo -e esp32_discrete_only
```

Flashing commands are listed in [../FLASHING.md](../FLASHING.md).
