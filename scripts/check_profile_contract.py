#!/usr/bin/env python3
"""Verify the four-profile preprocessor contract and dependency failures."""

from __future__ import annotations

import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "src" / "Esp32BaseProfile.h"
COMPILER = shutil.which("clang++") or shutil.which("g++")

if not COMPILER:
    raise SystemExit("error: clang++ or g++ is required")


def preprocess(defines: dict[str, str]) -> subprocess.CompletedProcess[str]:
    with tempfile.NamedTemporaryFile("w", suffix=".cpp", delete=False) as source:
        source.write(f'#include "{HEADER}"\n')
        source_path = Path(source.name)
    try:
        command = [COMPILER, "-std=c++17", "-E", "-dM", str(source_path)]
        command.extend(f"-D{name}={value}" for name, value in defines.items())
        return subprocess.run(command, text=True, capture_output=True, check=False)
    finally:
        source_path.unlink(missing_ok=True)


def macros(result: subprocess.CompletedProcess[str]) -> dict[str, str]:
    values: dict[str, str] = {}
    for line in result.stdout.splitlines():
        if not line.startswith("#define "):
            continue
        parts = line.split(maxsplit=2)
        if len(parts) == 3:
            values[parts[1]] = parts[2]
    return values


def expect_profile(profile: str, expected: dict[str, str]) -> None:
    result = preprocess({"ESP32BASE_PROFILE": profile})
    if result.returncode:
        raise AssertionError(f"{profile} preprocessing failed:\n{result.stderr}")
    actual = macros(result)
    for name, value in expected.items():
        if actual.get(name) != value:
            raise AssertionError(f"{profile}: {name} expected {value}, got {actual.get(name)}")
    for removed in (
        "ESP32BASE_ENABLE_WEB_OTA",
        "ESP32BASE_ENABLE_ARDUINO_OTA",
        "ESP32BASE_PROFILE_CORE",
        "ESP32BASE_PROFILE_RUNTIME",
        "ESP32BASE_PROFILE_NET",
        "ESP32BASE_PROFILE_NET_RUNTIME",
        "ESP32BASE_PROFILE_WEB",
        "ESP32BASE_PROFILE_WEB_RUNTIME",
        "ESP32BASE_PROFILE_FULL",
    ):
        if removed in actual:
            raise AssertionError(f"{profile}: removed macro remains defined: {removed}")


def expect_failure(defines: dict[str, str], message: str) -> None:
    result = preprocess(defines)
    if result.returncode == 0 or message not in result.stderr:
        raise AssertionError(
            f"expected preprocessing failure containing {message!r}; "
            f"returncode={result.returncode}\nstderr={result.stderr}"
        )


BASE_DISABLED = {
    "ESP32BASE_ENABLE_BUS": "0",
    "ESP32BASE_ENABLE_SLEEP": "0",
    "ESP32BASE_ENABLE_RTC": "0",
    "ESP32BASE_ENABLE_RS485_PORT": "0",
    "ESP32BASE_ENABLE_RECORD_STORE": "0",
    "ESP32BASE_ENABLE_APP_EVENTS": "0",
    "ESP32BASE_ENABLE_APP_CONFIG": "0",
}

expect_profile(
    "ESP32BASE_PROFILE_MINIMAL",
    BASE_DISABLED
    | {
        "ESP32BASE_ENABLE_WATCHDOG": "0",
        "ESP32BASE_ENABLE_FS": "0",
        "ESP32BASE_ENABLE_FILELOG": "0",
        "ESP32BASE_ENABLE_HEALTH": "0",
        "ESP32BASE_ENABLE_WIFI": "0",
        "ESP32BASE_ENABLE_WEB": "0",
        "ESP32BASE_ENABLE_OTA": "0",
        "ESP32BASE_ENABLE_MQTT": "0",
    },
)
expect_profile(
    "ESP32BASE_PROFILE_OFFLINE",
    BASE_DISABLED
    | {
        "ESP32BASE_ENABLE_WATCHDOG": "1",
        "ESP32BASE_ENABLE_FS": "1",
        "ESP32BASE_ENABLE_FILELOG": "1",
        "ESP32BASE_ENABLE_HEALTH": "1",
        "ESP32BASE_ENABLE_WIFI": "0",
        "ESP32BASE_ENABLE_WEB": "0",
        "ESP32BASE_ENABLE_OTA": "0",
        "ESP32BASE_ENABLE_MQTT": "0",
    },
)
local = BASE_DISABLED | {
    "ESP32BASE_ENABLE_WATCHDOG": "1",
    "ESP32BASE_ENABLE_FS": "1",
    "ESP32BASE_ENABLE_FILELOG": "1",
    "ESP32BASE_ENABLE_HEALTH": "1",
    "ESP32BASE_ENABLE_WIFI": "1",
    "ESP32BASE_ENABLE_DNS": "1",
    "ESP32BASE_ENABLE_NTP": "1",
    "ESP32BASE_ENABLE_MDNS": "1",
    "ESP32BASE_ENABLE_WEB": "1",
    "ESP32BASE_ENABLE_OTA": "1",
    "ESP32BASE_ENABLE_MQTT": "0",
}
expect_profile("ESP32BASE_PROFILE_LOCAL", local)
expect_profile("ESP32BASE_PROFILE_IOT", local | {"ESP32BASE_ENABLE_MQTT": "1"})

# Explicit application overrides remain authoritative.
override = preprocess(
    {
        "ESP32BASE_PROFILE": "ESP32BASE_PROFILE_LOCAL",
        "ESP32BASE_ENABLE_OTA": "0",
        "ESP32BASE_ENABLE_FS": "0",
        "ESP32BASE_ENABLE_FILELOG": "0",
    }
)
if override.returncode:
    raise AssertionError(f"explicit overrides failed:\n{override.stderr}")
override_macros = macros(override)
for name in ("ESP32BASE_ENABLE_OTA", "ESP32BASE_ENABLE_FS", "ESP32BASE_ENABLE_FILELOG"):
    if override_macros.get(name) != "0":
        raise AssertionError(f"explicit override lost for {name}")

expect_failure({"ESP32BASE_PROFILE": "99"}, "ESP32BASE_PROFILE must be one of")
expect_failure(
    {"ESP32BASE_ENABLE_ARDUINO_OTA": "0"},
    "Legacy OTA macros were removed",
)
expect_failure(
    {"ESP32BASE_PROFILE_FULL": "7"},
    "Legacy Esp32Base Profile macros were removed",
)
expect_failure(
    {
        "ESP32BASE_PROFILE": "ESP32BASE_PROFILE_MINIMAL",
        "ESP32BASE_ENABLE_CONFIG": "0",
    },
    "Log, Config, and System are mandatory",
)
expect_failure(
    {
        "ESP32BASE_PROFILE": "ESP32BASE_PROFILE_LOCAL",
        "ESP32BASE_ENABLE_WEB": "0",
    },
    "ESP32BASE_ENABLE_OTA requires ESP32BASE_ENABLE_WIFI and ESP32BASE_ENABLE_WEB",
)

print("Profile contract checks passed")
