# App Events Demo

This sample enables `ESP32BASE_ENABLE_APP_EVENTS=1` on `ESP32BASE_PROFILE_FULL`.

It writes several generic application events during boot, then exposes:

- `/esp32base/app-events` for the built-in App Events page, which shows the event log from a low-level store view including record status and internal fields
- `/esp32base/app-events?level=warn&time=real` for common UI filters
- `/esp32base/api/app-events?offset=0&limit=50&q=demo` for filtered paged JSON
- `/esp32base/app-events.csv?source=demo` for filtered CSV export
- `/demo/events` for a small demo page with a POST button that writes one more event

Application code that needs a business event list or business event detail page should use `Esp32BaseAppEventLog::readLatest()` or `/esp32base/api/app-events`, then map `source/type/reason/code/value` to business language. Business pages normally should not show internal store fields such as `magic`, `crc16`, `reserved`, `valueMask`, or `flags`; those belong to the built-in App Events page for maintenance and development.

The Time column shows wall-clock time when NTP or the current boot mapping can resolve it. Otherwise it shows `uptime N ms` plus the boot id, so relative startup events are not confused with real dates.

Default Web auth is `admin` / `admin`.

Build:

```sh
pio run -d examples/app_events_demo
```

Flashing commands are listed in [../FLASHING.md](../FLASHING.md).
