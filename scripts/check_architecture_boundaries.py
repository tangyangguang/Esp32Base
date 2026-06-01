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

for needle, message in (
    ("PreLifecycleHook", "System must expose a low-level lifecycle hook type"),
    ("setPreRestartHook", "System must allow facade-level pre-restart orchestration"),
    ("setPreSleepHook", "System must allow facade-level pre-sleep orchestration"),
):
    if needle not in system_header:
        errors.append(f"src/core/Esp32BaseSystem.h: {message}")

for needle, message in (
    ("runPreRestartHook();", "restart must run the registered pre-restart hook"),
    ("void Esp32BaseSystem::setPreRestartHook", "pre-restart hook setter must be implemented"),
    ("void Esp32BaseSystem::setPreSleepHook", "pre-sleep hook setter must be implemented"),
):
    if needle not in system:
        errors.append(f"src/core/Esp32BaseSystem.cpp: {message}")

for needle, message in (
    ("Esp32BaseSystem::setPreRestartHook(flushRuntimeBeforeLifecycleStop);", "facade must install the restart lifecycle hook"),
    ("Esp32BaseSystem::setPreSleepHook(flushRuntimeBeforeLifecycleStop);", "facade must install the sleep lifecycle hook"),
):
    if needle not in base:
        errors.append(f"src/Esp32Base.cpp: {message}")

if "Esp32BaseFileLog" in sleep:
    errors.append("src/runtime/Esp32BaseSleep.inc: sleep must use the system pre-sleep hook instead of depending on FileLog")
if "runPreSleepHook();" not in sleep:
    errors.append("src/runtime/Esp32BaseSleep.inc: deep sleep must run the registered pre-sleep hook")

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
