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


def require_absent(path: str, needle: str, message: str) -> None:
    target = ROOT / path
    if not target.exists():
        errors.append(f"{path}: missing file")
        return
    if needle in read(path):
        errors.append(f"{path}: {message}")


def function_body(text: str, signature: str) -> str:
    start = text.find(signature)
    if start < 0:
        return ""
    brace = text.find("{", start)
    if brace < 0:
        return ""
    depth = 0
    for i in range(brace, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return text[brace : i + 1]
    return ""


require("src/Esp32BaseProfile.h", "ESP32BASE_ENABLE_APP_EVENTS", "missing default-off app event macro")
require("src/Esp32BaseProfile.h", "ESP32BASE_ENABLE_APP_EVENTS && !ESP32BASE_ENABLE_FS", "app events must require FS")
require("src/runtime/Esp32BaseAppEventLog.h", "class Esp32BaseAppEventLog", "missing public app event log API")
require("src/runtime/Esp32BaseAppEventLog.h", "struct StoreRecord", "missing low-level app event store record view")
require("src/runtime/Esp32BaseAppEventLog.h", "readStoreRecords", "built-in event log page must use store-level records, not business-only reads")
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
    "Esp32BaseLongOperation::LongOperationScope",
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
require(
    "src/runtime/Esp32BaseAppEventLog.inc",
    'appEventMarkFault("size_mismatch")',
    "existing app event store with mismatched size must fault instead of being rebuilt",
)
if 'appEventMarkFault("record_crc_failed")' in read("src/runtime/Esp32BaseAppEventLog.inc"):
    errors.append("src/runtime/Esp32BaseAppEventLog.inc: single record CRC damage must not put the whole store in fault")
app_events_source = read("src/runtime/Esp32BaseAppEventLog.inc")
ensure_store_body = function_body(app_events_source, "bool appEventEnsureStoreFile()")
if "appEventRemoveStoreFile()" in ensure_store_body:
    errors.append("src/runtime/Esp32BaseAppEventLog.inc: begin must not remove/rebuild an existing app event store")
for signature in (
    "bool Esp32BaseAppEventLog::readLatest",
    "bool Esp32BaseAppEventLog::readStoreInfo",
    "bool Esp32BaseAppEventLog::readStoreRecords",
):
    body = function_body(app_events_source, signature)
    if "begin()" in body:
        errors.append(f"src/runtime/Esp32BaseAppEventLog.inc: {signature} must not call begin() from a read-only path")
require("src/Esp32Base.cpp", "Esp32BaseAppEventLog::begin()", "base begin must initialize app events")
require("src/web/Esp32BaseWeb.h", "BUILTIN_APP_EVENTS", "built-in labels must expose App Events")
require("src/web/Esp32BaseWeb.h", "checkPostAllowed", "public Web API must expose POST auth + same-origin helper")
require("src/web/internal/WebRouting.cpp", "g_currentMethod != Esp32BaseWeb::METHOD_POST", "checkPostAllowed must reject non-POST methods")
require("src/web/internal/WebRouting.cpp", "g_server.send(405", "checkPostAllowed must return 405 for non-POST methods")
require("src/web/internal/WebAppEvents.cpp", "handleAppEventsPage", "missing App Events page")
require("src/web/internal/WebAppEvents.cpp", "/esp32base/api/app-events", "missing API route marker")
require("src/web/internal/WebAppEvents.cpp", "struct AppEventFilter", "App Events page must support common filters")
require("src/web/internal/WebAppEvents.cpp", "status", "App Events built-in page must show store record status")
require("src/web/internal/WebAppEvents.cpp", "valueMask", "App Events details must show internal valueMask")
require("src/web/internal/WebAppEvents.cpp", "reserved", "App Events details must show internal reserved")
require("src/web/internal/WebAppEvents.cpp", "crc16", "App Events details must show stored crc16")
require("src/web/internal/WebAppEvents.cpp", "calculatedCrc16", "App Events details must show calculated crc16")
require("src/web/internal/WebAppEvents.cpp", "data-eb-light-dismiss", "App Events read-only details should support backdrop close")
require("src/web/internal/WebAppEvents.cpp", "Export CSV", "filtered CSV export must be grouped with filters")
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
require("src/web/internal/WebAssets.cpp", ".evdetailgrid", "App Events detail dialog must have internal field styling")
require("src/web/internal/WebAssets.cpp", ".appevfiltergrid", "App Events filters must use dedicated compact spacing")
require("src/web/internal/WebAssets.cpp", "dialog[data-eb-light-dismiss]", "read-only native dialogs must support opt-in backdrop close")
if "countFilteredEvents" in read("src/web/internal/WebAppEvents.cpp"):
    errors.append("src/web/internal/WebAppEvents.cpp: filters must not do a separate full count scan")
require("src/web/internal/WebAppEvents.cpp", "# error,", "CSV export must make read failures visible")
require("src/web/internal/WebLayout.cpp", "/esp32base/app-events", "system nav must link App Events")
require("src/web/internal/WebTools.cpp", "handleToolsAppEventsClearPost", "System page must own Clear App Events danger action")
require("src/web/internal/WebTools.cpp", "App Events cleared", "System page must report cleared App Events")
require("src/web/internal/WebTools.cpp", "Esp32BaseAppEventLog::clear()", "format FS must recreate App Events store after remount")
require("src/web/internal/WebTools.cpp", "app_events_recreate", "format FS logs must expose App Events recreate result")
if "/esp32base/app-events/clear" in read("src/web/Esp32BaseWeb.cpp") or "/esp32base/app-events/clear" in read("src/web/internal/WebAppEvents.cpp"):
    errors.append("App Events clear action must not remain on the App Events page or route")
require("src/web/Esp32BaseWeb.cpp", "/esp32base/tools/app-events-clear", "Clear App Events route must live under System tools")
require("src/web/Esp32BaseWeb.cpp", "first == '=' || first == '+' || first == '-' || first == '@'", "CSV export must guard spreadsheet formula prefixes")
require("src/web/internal/WebLayout.cpp", "pagination.perPage == 0 ? 10", "pagination default must be 10")
require("src/web/Esp32BaseWeb.cpp", "15, 20, 30, 50", "pagination options must include 10/15/20/30/50")
if "15, 20, 30, 50, 100" in read("src/web/Esp32BaseWeb.cpp"):
    errors.append("src/web/Esp32BaseWeb.cpp: pagination options must not include 100")
require("src/web/internal/WebLogs.cpp", "Esp32BaseFileLog", "System Logs page must remain FileLog-oriented")
if (ROOT / "src/web/internal/WebLogs.cpp").exists() and "Esp32BaseAppEventLog" in read("src/web/internal/WebLogs.cpp"):
    errors.append("src/web/internal/WebLogs.cpp: System Logs page must not mix in App Events")
require("src/web/internal/WebFs.cpp", "appEventsOwnsPath", "FS management must recognize App Events store as an internal path")
require("src/web/internal/WebFs.cpp", "targetIsAppEvents", "FS upload/delete must detect App Events store for runtime reload")
require("src/runtime/Esp32BaseAppEventLog.h", "static bool reload();", "App Events must expose an explicit runtime reload API")
require("src/runtime/Esp32BaseAppEventLog.inc", "bool Esp32BaseAppEventLog::reload()", "App Events reload implementation is required")
require("src/web/internal/WebFs.cpp", "Esp32BaseAppEventLog::reload();", "FS upload/delete of App Events store must reload runtime state")
if "Target is reserved for App Events" in read("src/web/internal/WebFs.cpp"):
    errors.append("src/web/internal/WebFs.cpp: FS upload must not reject App Events store during test/maintenance imports")
web_fs_source = read("src/web/internal/WebFs.cpp")
delete_body = function_body(web_fs_source, "void handleFsDeletePost()")
if "Esp32BaseFs::fileSize(path) == 0" not in delete_body or "Esp32BaseAppEventLog::clear()" not in delete_body:
    errors.append("src/web/internal/WebFs.cpp: App Events delete path must rebuild the store when removeFile() only truncates it to 0 bytes")
if "if (ok && targetIsAppEvents)" in delete_body:
    errors.append("src/web/internal/WebFs.cpp: App Events delete reload must not be gated by FileLog reload mutating ok")
if "const bool deleteOk = ok;" not in delete_body:
    errors.append("src/web/internal/WebFs.cpp: delete post-processing must preserve raw delete result before runtime reloads")
web_logs_source = read("src/web/internal/WebLogs.cpp")
for signature in ("void handleLogsPage", "void handleLogsRaw"):
    body = function_body(web_logs_source, signature)
    if "Esp32BaseFileLog::flush()" in body:
        errors.append(f"src/web/internal/WebLogs.cpp: {signature} must not flush or write from GET/read-only paths")
require("README.md", "ESP32BASE_ENABLE_APP_EVENTS", "README must document enabling app events")
require("README.md", "不是业务长期数据模型", "README must clearly distinguish App Events from long-term business data")
require("README.md", "System Diagnostic Logs（系统诊断日志", "README must name FileLog as system diagnostic logs")
require("README.md", "系统诊断日志写技术事实和内部错误链路", "README must allow dual recording with separate meanings")
require("CHANGELOG.md", "应用事件日志", "CHANGELOG must record app event log capability")
require("CHANGELOG.md", "近期关键事件窗口", "CHANGELOG must document the intended App Events usage boundary")
require("CHANGELOG.md", "不是第二套系统诊断日志", "CHANGELOG must warn business apps not to duplicate system logs")
require("CHANGELOG.md", "默认 Web 标签从 `Logs` 调整为 `System Logs`", "CHANGELOG must document the user-facing logs label")
require("CHANGELOG.md", "系统诊断日志写技术事实和内部错误链路", "CHANGELOG must clarify FileLog/App Events split")
require("docs/03_api.md", "Esp32BaseAppEventLog", "API docs must include app event log")
require("docs/03_api.md", "不是业务长期数据模型", "API docs must document App Events storage boundary")
require("docs/03_api.md", "System Diagnostic Logs（系统诊断日志", "API docs must define FileLog vs App Events decision rule")
require("docs/03_api.md", "不能机械重复", "API docs must allow dual logging only with different semantics")
require("docs/03_api.md", "业务事件列表", "API docs must explain app-owned event list/detail pages")
require("docs/03_api.md", "readStoreRecords", "API docs must separate low-level built-in event log reading from business reads")
require("docs/04_web.md", "/esp32base/app-events", "Web docs must include App Events page")
require("docs/04_web.md", "事件日志", "Web docs must name App Events as event log, not diagnostic page")
require("docs/04_web.md", "System Logs", "Web docs must expose the user-facing system logs label")
require("docs/04_web.md", "不应把 boot/reset", "Web docs must prevent duplicating system events in App Events")
require("docs/04_web.md", "上传或删除后会重新加载 App Events 运行态", "Web docs must document App Events store maintenance reload")
require("docs/03_api.md", "loop/system task", "API docs must document App Events task ownership boundary")
require("docs/03_api.md", "checkPostAllowed", "API docs must document POST same-origin helper")
require("docs/06_memory_budget.md", "188 KiB", "memory budget must include default app event storage")
require("docs/06_memory_budget.md", "不允许覆盖的业务数据", "memory budget must say App Events are not permanent business history")
require("docs/07_diagnostics.md", "App Events", "diagnostics docs must include verification")
require("docs/07_diagnostics.md", "不重复记录 Esp32Base 系统事件", "diagnostics docs must verify App Events do not duplicate system logs")
require("docs/07_diagnostics.md", "App Event 必须表达业务影响", "diagnostics docs must verify dual recording semantics")
require("docs/10_known_limitations.md", "App Events 边界", "known limitations must document App Events boundary")
require("docs/10_known_limitations.md", "不是第二套系统诊断日志", "known limitations must separate App Events from FileLog")
require("docs/09_release_checklist.md", "record_skipped", "release checklist must use current damaged-record skip semantics")
require("examples/app_events_demo/src/main.cpp", "Esp32BaseAppEventLog::append", "sample must write app events")
require("examples/app_events_demo/src/main.cpp", "checkPostAllowed", "sample POST route must use auth + same-origin helper")
require_absent("examples/app_events_demo/src/main.cpp", '"boot"', "sample App Events must not teach apps to log system boot events")
require_absent("examples/app_events_demo/src/main.cpp", "bootCount", "sample App Events must not copy system boot counters into business events")
require("examples/app_events_demo/README.md", "/esp32base/app-events", "sample README must explain viewing page")
require("examples/app_events_demo/README.md", "business event list", "sample README must explain app-owned business event pages")
full_demo_source = read("examples/full_demo/src/main.cpp")
if "checkPostAllowed(\"full_demo_control\")" not in function_body(full_demo_source, "void handleControlApi"):
    errors.append("examples/full_demo/src/main.cpp: /api/control POST branch must use checkPostAllowed()")
if "checkPostAllowed(\"full_demo_ui_action\")" not in function_body(full_demo_source, "void handleUiActionRun"):
    errors.append("examples/full_demo/src/main.cpp: /ui-action/run must use checkPostAllowed()")
require("examples/full_demo/src/main.cpp", "RUN_CROSS_ORIGIN_SELFTEST", "full_demo selftest must cover hostile Origin POST rejection")
gallery_source = read("examples/web_ui_gallery/src/main.cpp")
if "checkPostAllowed(\"gallery_post\")" not in function_body(gallery_source, "void handlePostRedirect"):
    errors.append("examples/web_ui_gallery/src/main.cpp: gallery redirect POST helper must use checkPostAllowed()")
if "checkPostAllowed(\"gallery_config_name\")" not in function_body(gallery_source, "void handleConfigNameSave"):
    errors.append("examples/web_ui_gallery/src/main.cpp: gallery name save must use checkPostAllowed()")
if "checkPostAllowed(\"gallery_config_dialog\")" not in function_body(gallery_source, "void handleConfigDialogSave"):
    errors.append("examples/web_ui_gallery/src/main.cpp: gallery dialog save must use checkPostAllowed()")
require("examples/web_ui_gallery/src/main.cpp", "RUN_CROSS_ORIGIN_SELFTEST", "web_ui_gallery selftest must cover hostile Origin POST rejection")

if errors:
    for error in errors:
        print(error)
    raise SystemExit(1)

print("App event checks passed")
