#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


errors = []

system = read("src/core/Esp32BaseSystem.cpp")
system_header = read("src/core/Esp32BaseSystem.h")
sleep = read("src/runtime/Esp32BaseSleep.inc")

for forbidden in ("../runtime/", "Esp32BaseFileLog"):
    if forbidden in system:
        errors.append(f"src/core/Esp32BaseSystem.cpp: core system must not depend on {forbidden}")

for forbidden in ("setPreRestartHook", "setPreSleepHook"):
    if forbidden in system_header:
        errors.append(f"src/core/Esp32BaseSystem.h: lifecycle hook setters must not be public Esp32BaseSystem API ({forbidden})")

if "Esp32BaseFileLog" in sleep:
    errors.append("src/runtime/Esp32BaseSleep.inc: sleep must use the system pre-sleep hook instead of depending on FileLog")

if errors:
    for error in errors:
        print(error)
    raise SystemExit(1)

print("Architecture boundary checks passed")
