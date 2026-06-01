#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


errors = []
watchdog = read("src/runtime/Esp32BaseWatchdog.inc")
long_operation = read("src/runtime/internal/Esp32BaseLongOperation.h")

if "esp_task_wdt_delete" in watchdog:
    errors.append("src/runtime/Esp32BaseWatchdog.inc: long operations must not delete the current task from WDT")
if "return;\n    }\n    {" in watchdog and "g_removedTask" in watchdog:
    errors.append("src/runtime/Esp32BaseWatchdog.inc: feed() must not skip the current task during long operations")
for needle, message in (
    ("TaskHandle_t g_longOperationTask", "watchdog must track the current long-operation task without unregistering it"),
    ("uint8_t g_longOperationDepth", "watchdog must track nested long-operation scopes"),
    ("currentTaskInLongOperation", "watchdog must expose audited long-operation scope state"),
):
    if needle not in watchdog:
        errors.append(f"src/runtime/Esp32BaseWatchdog.inc: {message}")
if "removeCurrentTaskForLongOperation" not in long_operation:
    errors.append("src/runtime/internal/Esp32BaseLongOperation.h: scope must retain compatibility with existing watchdog API")

docs = {
    "docs/07_diagnostics.md": "长操作不再从 task WDT 注销当前任务",
    "docs/10_known_limitations.md": "不可细分的底层调用仍可能触发 WDT",
    "CHANGELOG.md": "Watchdog 长操作策略改为保留 WDT 注册",
}
for path, needle in docs.items():
    if needle not in read(path):
        errors.append(f"{path}: missing watchdog policy marker {needle!r}")

if errors:
    for error in errors:
        print(error)
    raise SystemExit(1)

print("Watchdog policy checks passed")
