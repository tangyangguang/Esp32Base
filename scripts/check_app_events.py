#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


errors = []


def require(path: str, needle: str, message: str) -> None:
    target = ROOT / path
    if not target.exists():
        errors.append(f"{path}: missing file")
        return
    text = read(path)
    if needle not in text:
        errors.append(f"{path}: {message}")


require("src/Esp32BaseProfile.h", "ESP32BASE_ENABLE_APP_EVENTS", "missing default-off app event macro")
require("src/Esp32BaseProfile.h", "ESP32BASE_ENABLE_APP_EVENTS && !ESP32BASE_ENABLE_FS", "app events must require FS")
require("src/runtime/Esp32BaseAppEventLog.h", "class Esp32BaseAppEventLog", "missing public app event log API")
require(
    "src/runtime/Esp32BaseAppEventLog.h",
    "static_assert(sizeof(Esp32BaseAppEventRecord) == 188",
    "record layout must stay 188 bytes",
)
require("src/runtime/Esp32BaseAppEventLog.inc", "Header A", "storage implementation must document/use dual headers")
require("src/runtime/Esp32BaseAppEventLog.inc", "crc16", "records must have crc16 validation")
require(
    "src/runtime/Esp32BaseAppEventLog.inc",
    "appEventReconcileLoadedStore",
    "app event begin must reconcile full-ring interrupted overwrites and visible record damage",
)
require(
    "src/runtime/Esp32BaseAppEventLog.inc",
    "appEventCommittedForHeader",
    "app events must ignore uncommitted future records after power loss",
)
require(
    "src/runtime/Esp32BaseAppEventLog.inc",
    "removeCurrentTaskForLongOperation",
    "app event FS operations must protect loopTask watchdog",
)
require(
    "src/runtime/Esp32BaseAppEventLog.inc",
    "appEventUpdateValidCountAfterScan",
    "full read scans must tighten count() after runtime record damage",
)
require(
    "src/runtime/Esp32BaseAppEventLog.inc",
    "appEventEnsureDir",
    "clear must recreate /app before rebuilding the fixed store",
)
if 'appEventMarkFault("record_crc_failed")' in read("src/runtime/Esp32BaseAppEventLog.inc"):
    errors.append("src/runtime/Esp32BaseAppEventLog.inc: single record CRC damage must not put the whole store in fault")
require("src/Esp32Base.cpp", "Esp32BaseAppEventLog::begin()", "base begin must initialize app events")
require("src/web/Esp32BaseWeb.h", "BUILTIN_APP_EVENTS", "built-in labels must expose App Events")
require("src/web/Esp32BaseWeb.h", "checkPostAllowed", "public Web API must expose POST auth + same-origin helper")
require("src/web/internal/WebAppEvents.cpp", "handleAppEventsPage", "missing App Events page")
require("src/web/internal/WebAppEvents.cpp", "/esp32base/api/app-events", "missing API route marker")
require("src/web/internal/WebAppEvents.cpp", "struct AppEventFilter", "App Events page must support common filters")
require("src/web/internal/WebAppEvents.cpp", "validFilterToken", "exact App Events filters must validate token length/chars before matching")
require("src/web/internal/WebAppEvents.cpp", "invalid_filter", "invalid App Events filters must return an explicit client error")
if 'copyArg("source"' in read("src/web/internal/WebAppEvents.cpp"):
    errors.append("src/web/internal/WebAppEvents.cpp: exact source filter must not be silently truncated")
require("src/web/internal/WebAppEvents.cpp", "struct AppEventScanState", "App Events filters must scan once per response")
require("src/web/internal/WebAppEvents.cpp", "resolveAppEventEpoch", "App Events must resolve/display real time when possible")
require("src/web/internal/WebAppEvents.cpp", "uptimeMs", "App Events must expose uptime in milliseconds")
require("src/web/internal/WebAppEvents.cpp", "uptimeSec", "App Events API/CSV must retain stable uptimeSec")
require("src/web/internal/WebAppEvents.cpp", "sendFilterTimeOption(\"real\"", "App Events filters must include real-time events")
require("src/web/internal/WebAssets.cpp", ".evtable", "App Events table must have dedicated readable styling")
if "countFilteredEvents" in read("src/web/internal/WebAppEvents.cpp"):
    errors.append("src/web/internal/WebAppEvents.cpp: filters must not do a separate full count scan")
require("src/web/internal/WebAppEvents.cpp", "# error,", "CSV export must make read failures visible")
require("src/web/internal/WebLayout.cpp", "/esp32base/app-events", "system nav must link App Events")
require("src/web/internal/WebLogs.cpp", "Esp32BaseFileLog", "Logs page must remain FileLog-oriented")
if (ROOT / "src/web/internal/WebLogs.cpp").exists() and "Esp32BaseAppEventLog" in read("src/web/internal/WebLogs.cpp"):
    errors.append("src/web/internal/WebLogs.cpp: Logs page must not mix in App Events")
require("README.md", "ESP32BASE_ENABLE_APP_EVENTS", "README must document enabling app events")
require("CHANGELOG.md", "应用事件日志", "CHANGELOG must record app event log capability")
require("docs/03_api.md", "Esp32BaseAppEventLog", "API docs must include app event log")
require("docs/04_web.md", "/esp32base/app-events", "Web docs must include App Events page")
require("docs/03_api.md", "loop/system task", "API docs must document App Events task ownership boundary")
require("docs/03_api.md", "checkPostAllowed", "API docs must document POST same-origin helper")
require("docs/06_memory_budget.md", "188 KiB", "memory budget must include default app event storage")
require("docs/07_diagnostics.md", "App Events", "diagnostics docs must include verification")
require("docs/09_release_checklist.md", "record_skipped", "release checklist must use current damaged-record skip semantics")
require("examples/app_events_demo/src/main.cpp", "Esp32BaseAppEventLog::append", "sample must write app events")
require("examples/app_events_demo/src/main.cpp", "checkPostAllowed", "sample POST route must use auth + same-origin helper")
require("examples/app_events_demo/README.md", "/esp32base/app-events", "sample README must explain viewing page")

if errors:
    for error in errors:
        print(error)
    raise SystemExit(1)

print("App event checks passed")
