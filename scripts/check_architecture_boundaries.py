#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


errors = []

system = read("src/core/Esp32BaseSystem.cpp")
system_header = read("src/core/Esp32BaseSystem.h")
base = read("src/Esp32Base.cpp")
sleep = read("src/runtime/Esp32BaseSleep.inc")

for forbidden in ("../runtime/", "Esp32BaseFileLog"):
    if forbidden in system:
        errors.append(f"src/core/Esp32BaseSystem.cpp: core system must not depend on {forbidden}")

for forbidden in ("setPreRestartHook", "setPreSleepHook"):
    if forbidden in system_header:
        errors.append(f"src/core/Esp32BaseSystem.h: lifecycle hook setters must not be public Esp32BaseSystem API ({forbidden})")

for needle, message in (
    ("namespace esp32base_internal", "System must keep lifecycle hooks in the internal namespace"),
    ("registerPreRestartHook", "System must support internal pre-restart hook registration"),
    ("registerPreSleepHook", "System must support internal pre-sleep hook registration"),
):
    if needle not in system_header:
        errors.append(f"src/core/Esp32BaseSystem.h: {message}")

for needle, message in (
    ("runPreRestartHooks();", "restart must run registered pre-restart hooks"),
    ("bool registerPreRestartHook", "pre-restart hook registration must be implemented"),
    ("bool registerPreSleepHook", "pre-sleep hook registration must be implemented"),
    ("constexpr uint8_t LIFECYCLE_HOOK_CAPACITY", "lifecycle hooks must support more than one internal registrant"),
):
    if needle not in system:
        errors.append(f"src/core/Esp32BaseSystem.cpp: {message}")

for needle, message in (
    ("esp32base_internal::registerPreRestartHook(flushRuntimeBeforeLifecycleStop);", "facade must install the restart lifecycle hook internally"),
    ("esp32base_internal::registerPreSleepHook(flushRuntimeBeforeLifecycleStop);", "facade must install the sleep lifecycle hook internally"),
):
    if needle not in base:
        errors.append(f"src/Esp32Base.cpp: {message}")

if "Esp32BaseFileLog" in sleep:
    errors.append("src/runtime/Esp32BaseSleep.inc: sleep must use the system pre-sleep hook instead of depending on FileLog")
if "esp32base_internal::runPreSleepHooks();" not in sleep:
    errors.append("src/runtime/Esp32BaseSleep.inc: deep sleep must run registered pre-sleep hooks")

docs = {
    "docs/01_architecture.md": "Core 不 include Runtime/FileLog",
    "docs/01_architecture.md": "OTA boot 初始化",
    "CHANGELOG.md": "Core/Runtime 生命周期边界收敛",
}
for path, needle in docs.items():
    if needle not in read(path):
        errors.append(f"{path}: missing architecture boundary marker {needle!r}")

if errors:
    for error in errors:
        print(error)
    raise SystemExit(1)

print("Architecture boundary checks passed")
