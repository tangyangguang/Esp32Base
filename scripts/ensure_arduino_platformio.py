#!/usr/bin/env python3
"""Install and verify both isolated Arduino ESP32 Core build homes."""

from __future__ import annotations

import json
import os
from pathlib import Path
import shutil
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
BASIC_EXAMPLE = ROOT / "examples" / "basic"

CONFIGS = {
    "2": {
        "envs": ("esp32_minimal", "esp32s3_minimal", "esp32c3_minimal"),
        "platform_version": "6.7.0",
        "framework_version": "3.20016.0",
        "required": (
            Path("framework-arduinoespressif32/package.json"),
            Path("framework-arduinoespressif32/cores/esp32/Arduino.h"),
            Path("framework-arduinoespressif32/libraries/Preferences/src/Preferences.h"),
            Path("framework-arduinoespressif32/libraries/WiFi/src/WiFi.h"),
            Path("toolchain-xtensa-esp32/bin/xtensa-esp32-elf-g++"),
            Path("toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-g++"),
            Path("toolchain-riscv32-esp/bin/riscv32-esp-elf-g++"),
        ),
    },
    "3": {
        "envs": ("esp32_minimal_arduino3", "esp32_local_arduino3"),
        "platform_version": "55.03.38",
        "framework_version": "3.3.8",
        "required": (
            Path("framework-arduinoespressif32/package.json"),
            Path("framework-arduinoespressif32/cores/esp32/Arduino.h"),
            Path("framework-arduinoespressif32/libraries/WiFi/src/WiFi.h"),
            Path("framework-arduinoespressif32/libraries/Network/src/NetworkClient.h"),
            Path("framework-arduinoespressif32/libraries/Hash/src/PBKDF2_HMACBuilder.h"),
            Path("framework-arduinoespressif32/tools/pioarduino-build.py"),
            Path("framework-arduinoespressif32-libs/package.json"),
            Path("framework-arduinoespressif32-libs/tools.json"),
            Path("framework-arduinoespressif32-libs/esp32/lib"),
            Path("toolchain-xtensa-esp-elf/bin/xtensa-esp32-elf-g++"),
        ),
    },
}


def run(pio: str, major: str, environment_name: str) -> None:
    core_dir = ROOT / ".piohome" / f"arduino{major}"
    core_dir.mkdir(parents=True, exist_ok=True)
    environment = os.environ.copy()
    environment["PLATFORMIO_CORE_DIR"] = str(core_dir)
    environment["PLATFORMIO_BUILD_DIR"] = str(
        BASIC_EXAMPLE / ".pio" / "build" / f"arduino{major}"
    )
    environment["PLATFORMIO_LIBDEPS_DIR"] = str(
        BASIC_EXAMPLE / ".pio" / "libdeps" / f"arduino{major}"
    )
    command = [
        pio,
        "pkg",
        "install",
        "-d",
        str(BASIC_EXAMPLE),
        "-e",
        environment_name,
        "--no-save",
    ]
    print(f"PLATFORMIO_CORE_DIR={core_dir}", flush=True)
    print("+ " + " ".join(command), flush=True)
    subprocess.run(command, cwd=ROOT, env=environment, check=True)


def manifest_version(path: Path, errors: list[str]) -> str:
    try:
        return str(json.loads(path.read_text(encoding="utf-8")).get("version", ""))
    except (OSError, json.JSONDecodeError) as error:
        errors.append(f"{path}: {error}")
        return ""


def verify(major: str) -> list[str]:
    config = CONFIGS[major]
    core_dir = ROOT / ".piohome" / f"arduino{major}"
    packages_dir = core_dir / "packages"
    errors: list[str] = []
    for relative in config["required"]:
        path = packages_dir / relative
        if not path.exists():
            errors.append(str(path))

    manifests = (
        (
            core_dir / "platforms" / "espressif32" / "platform.json",
            config["platform_version"],
        ),
        (
            packages_dir / "framework-arduinoespressif32" / "package.json",
            config["framework_version"],
        ),
    )
    for manifest, expected in manifests:
        if not manifest.exists():
            errors.append(str(manifest))
            continue
        actual = manifest_version(manifest, errors)
        if actual and actual != expected:
            errors.append(f"{manifest}: expected version {expected}, got {actual}")
    return errors


def main() -> int:
    pio = shutil.which("pio")
    if not pio:
        print("PlatformIO executable 'pio' was not found in PATH", file=sys.stderr)
        return 1

    for major, config in CONFIGS.items():
        for environment_name in config["envs"]:
            run(pio, major, environment_name)

    errors: list[str] = []
    for major in CONFIGS:
        errors.extend(verify(major))
    if errors:
        print("Isolated Arduino Core package verification failed:", file=sys.stderr)
        for error in errors:
            print(f"  {error}", file=sys.stderr)
        return 1

    print("Arduino Core 2.x packages are ready in .piohome/arduino2")
    print("Arduino Core 3.x packages are ready in .piohome/arduino3")
    print("Run PlatformIO through: python3 scripts/pio_arduino.py <2|3> ...")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
