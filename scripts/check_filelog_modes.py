#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path):
    return (ROOT / path).read_text(encoding="utf-8")


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
        "src/web/Esp32BaseWeb.inc",
        'ESP32BASE_LOG_W("web", "filelog_mode_requested source=tools',
        "FileLog mode request should stay WARN because it is a system-level configuration change",
    ),
    (
        "src/web/Esp32BaseWeb.inc",
        'if (raw == "error")',
        "Web FileLog parser does not accept error",
    ),
    (
        "src/web/Esp32BaseWeb.inc",
        'sendFileLogModeOption("error", "ERROR", Esp32BaseFileLog::ERROR);',
        "Web FileLog form does not show ERROR",
    ),
]


errors = []
for path, needle, message in checks:
    if needle not in read(path):
        errors.append(f"{path}: {message}")

docs = {
    "docs/03_api.md": "运行时文件日志模式只支持 OFF、ERROR、WARN、INFO",
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
