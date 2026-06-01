#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path):
    if path == "src/web/internal Web modules":
        return read_web_source()
    return (ROOT / path).read_text(encoding="utf-8")

WEB_SOURCE_PATHS = [
    "src/web/Esp32BaseWeb.cpp",
    "src/web/internal/WebInternal.h",
    "src/web/internal/WebContext.h",
    "src/web/internal/WebContext.cpp",
    "src/web/internal/WebAssets.cpp",
    "src/web/internal/WebAuth.cpp",
    "src/web/internal/WebFs.cpp",
    "src/web/internal/WebLayout.cpp",
    "src/web/internal/WebLogs.cpp",
    "src/web/internal/WebOta.cpp",
    "src/web/internal/WebResponse.cpp",
    "src/web/internal/WebRouting.cpp",
    "src/web/internal/WebStatus.cpp",
    "src/web/internal/WebTools.cpp",
    "src/web/internal/WebWifi.cpp",
    "src/web/internal/WebAppConfig.cpp",
]

def read_web_source() -> str:
    return "\n".join((ROOT / path).read_text(encoding="utf-8") for path in WEB_SOURCE_PATHS)


checks = [
    (
        "src/runtime/Esp32BaseFileLog.h",
        "#define ESP32BASE_FILELOG_MODE_ERROR ESP32BASE_LOG_ERROR",
        "missing public ERROR mode macro",
    ),
    (
        "src/runtime/Esp32BaseFileLog.h",
        "ERROR = ESP32BASE_FILELOG_MODE_ERROR",
        "missing Esp32BaseFileLog::ERROR enum value",
    ),
    (
        "src/runtime/Esp32BaseFileLog.h",
        "ESP32BASE_EB_FILELOG_DEFAULT_MODE != ESP32BASE_FILELOG_MODE_ERROR",
        "default mode validator does not allow ERROR",
    ),
    (
        "src/runtime/Esp32BaseFileLog.inc",
        "case Esp32BaseFileLog::ERROR: return \"ERROR\";",
        "modeName() does not expose ERROR",
    ),
    (
        "src/runtime/Esp32BaseFileLog.inc",
        "mode != Esp32BaseFileLog::ERROR",
        "validMode() does not allow ERROR",
    ),
    (
        "src/runtime/Esp32BaseFileLog.inc",
        "case Esp32BaseFileLog::ERROR: return Esp32BaseLog::ERROR;",
        "modeLevel() does not map ERROR",
    ),
    (
        "src/runtime/Esp32BaseFileLog.inc",
        "return applyMode(readMode(), false, false);",
        "begin() must load persisted FileLog mode without saving or logging mode_changed",
    ),
    (
        "src/runtime/Esp32BaseFileLog.inc",
        "return applyMode(mode, true, true);",
        "setMode() must be the explicit persist-and-log mode change path",
    ),
    (
        "src/runtime/Esp32BaseFileLog.h",
        "static bool faulted();",
        "missing FileLog runtime fault state accessor",
    ),
    (
        "src/runtime/Esp32BaseFileLog.inc",
        "bool Esp32BaseFileLog::faulted() { return g_fileLogFault; }",
        "missing FileLog faulted() implementation",
    ),
    (
        "src/web/internal Web modules",
        'Esp32BaseFileLog::faulted() ? "write fault"',
        "Status/Logs pages must distinguish runtime write fault from disabled mode",
    ),
    (
        "src/web/internal Web modules",
        "New FileLog writes are stopped after a FS write failure.",
        "Logs/System pages must explain that write fault does not mean old logs are unreadable",
    ),
    (
        "src/web/internal Web modules",
        "FileLog mode is OFF. Existing log files are historical; new logs are not written.",
        "Logs/System pages must make disabled FileLog state visible",
    ),
    (
        "src/web/internal Web modules",
        'sendFileLogRuntimeStateRow("Runtime state");',
        "Tools page must expose FileLog runtime state",
    ),
    (
        "src/web/internal Web modules",
        'ESP32BASE_LOG_W("web", "filelog_mode_requested source=tools',
        "FileLog mode request should stay WARN because it is a system-level configuration change",
    ),
    (
        "src/web/internal Web modules",
        'if (raw == "error")',
        "Web FileLog parser does not accept error",
    ),
    (
        "src/web/internal Web modules",
        'sendFileLogModeOption("error", "ERROR", Esp32BaseFileLog::ERROR);',
        "Web FileLog form does not show ERROR",
    ),
]


errors = []
for path, needle, message in checks:
    if needle not in read(path):
        errors.append(f"{path}: {message}")

docs = {
    "docs/03_api.md": "运行时系统诊断日志模式只支持 OFF、ERROR、WARN、INFO",
    "docs/04_web.md": "模式设置只接受 OFF、ERROR、WARN、INFO",
    "docs/07_diagnostics.md": "ERROR/WARN/INFO",
    "docs/09_release_checklist.md": "ERROR 仅在 FileLog 模式为 ERROR",
    "CHANGELOG.md": "FileLog 支持 ERROR 模式",
}

for path, needle in docs.items():
    if needle not in read(path):
        errors.append(f"{path}: missing documentation marker {needle!r}")

if errors:
    for error in errors:
        print(error)
    raise SystemExit(1)

print("FileLog mode checks passed")
