#!/usr/bin/env python3
"""Check the four supported profiles against final ELF symbols."""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_ENVS = ("esp32_minimal", "esp32_offline", "esp32_local", "esp32_iot")

MQTT_FORBIDDEN = ("Esp32BaseMqtt", "esp_mqtt_client_", "mqtt_task")
ARDUINO_OTA_FORBIDDEN = ("ArduinoOTA", "ArduinoOTAClass", "espota")

FORBIDDEN = {
    "esp32_minimal": (
        "WiFi", "DNSServer", "MDNS", "WebServer", "UpdateClass", "LittleFS"
    ) + MQTT_FORBIDDEN + ARDUINO_OTA_FORBIDDEN,
    "esp32_offline": (
        "WiFi", "DNSServer", "MDNS", "WebServer", "UpdateClass"
    ) + MQTT_FORBIDDEN + ARDUINO_OTA_FORBIDDEN,
    "esp32_local": MQTT_FORBIDDEN + ARDUINO_OTA_FORBIDDEN,
    "esp32_iot": ARDUINO_OTA_FORBIDDEN,
}

REQUIRED = {
    # MINIMAL/OFFLINE defaults are asserted at compile time in examples/basic;
    # LTO may inline their small facade methods and remove class-name symbols.
    "esp32_local": ("Esp32BaseWeb", "Esp32BaseOta", "UpdateClass"),
    "esp32_iot": ("Esp32BaseWeb", "Esp32BaseOta", "Esp32BaseMqtt", "esp_mqtt_client_"),
}


def find_nm(explicit: str | None) -> str:
    if explicit:
        return explicit
    for name in ("xtensa-esp32-elf-nm", "xtensa-esp-elf-nm", "riscv32-esp-elf-nm"):
        found = shutil.which(name)
        if found:
            return found
    package_roots = (
        ROOT / ".piohome" / "arduino2" / "packages",
        ROOT / ".piohome" / "arduino3" / "packages",
        Path.home() / ".platformio" / "packages",
    )
    for base in package_roots:
        for candidate in base.glob("toolchain-*/bin/*-nm"):
            return str(candidate)
    raise SystemExit("error: no ESP nm tool found; pass --nm /path/to/*-nm")


def read_symbols(nm: str, elf: Path) -> str:
    try:
        return subprocess.check_output([nm, "-C", str(elf)], text=True, errors="replace")
    except subprocess.CalledProcessError as exc:
        raise SystemExit(f"error: nm failed for {elf}: {exc}") from exc


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", default="examples/basic/.pio/build/arduino2")
    parser.add_argument("--nm", default=None)
    parser.add_argument("--elf", default=None, help="check one explicit ELF instead of build-dir/envs")
    parser.add_argument("--forbid", nargs="*", default=None, help="symbol substrings that must be absent")
    parser.add_argument("envs", nargs="*", default=DEFAULT_ENVS)
    args = parser.parse_args()

    nm = find_nm(args.nm)
    if args.elf:
        if args.forbid is None:
            print("error: --elf requires --forbid", file=sys.stderr)
            return 2
        elf = Path(args.elf)
        if not elf.exists():
            print(f"explicit: missing {elf}", file=sys.stderr)
            return 1
        symbols = read_symbols(nm, elf)
        hits = [pattern for pattern in args.forbid if pattern in symbols]
        if hits:
            print(f"{elf}: FAIL forbidden symbols: {', '.join(hits)}")
            return 1
        print(f"{elf}: OK")
        return 0

    build_dir = Path(args.build_dir)
    failed = False
    for env in args.envs:
        elf = build_dir / env / "firmware.elf"
        if not elf.exists():
            print(f"{env}: missing {elf}", file=sys.stderr)
            failed = True
            continue
        symbols = read_symbols(nm, elf)
        forbidden_hits = [pattern for pattern in FORBIDDEN.get(env, ()) if pattern in symbols]
        required_missing = [pattern for pattern in REQUIRED.get(env, ()) if pattern not in symbols]
        if forbidden_hits or required_missing:
            details = []
            if forbidden_hits:
                details.append(f"forbidden symbols: {', '.join(forbidden_hits)}")
            if required_missing:
                details.append(f"missing required symbols: {', '.join(required_missing)}")
            print(f"{env}: FAIL {'; '.join(details)}")
            failed = True
        else:
            print(f"{env}: OK")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
