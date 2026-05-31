# App Events Demo

This sample enables `ESP32BASE_ENABLE_APP_EVENTS=1` on `ESP32BASE_PROFILE_FULL`.

It writes several generic application events during boot, then exposes:

- `/esp32base/app-events` for the built-in App Events page
- `/esp32base/app-events?level=warn&time=real` for common UI filters
- `/esp32base/api/app-events?offset=0&limit=50&q=demo` for filtered paged JSON
- `/esp32base/app-events.csv?source=demo` for filtered CSV export
- `/demo/events` for a small demo page with a POST button that writes one more event

The Time column shows wall-clock time when NTP or the current boot mapping can resolve it. Otherwise it shows `uptime N ms` plus the boot id, so relative startup events are not confused with real dates.

Default Web auth is `admin` / `admin`.

Build:

```sh
pio run -d examples/app_events_demo
```

Flashing commands are listed in [../FLASHING.md](../FLASHING.md).
