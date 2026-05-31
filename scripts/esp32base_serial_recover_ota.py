#!/usr/bin/env python3
"""esp32base_serial_recover_ota: recover dual-OTA ESP32 devices over serial.

The script builds a PlatformIO environment, reads its partition table, writes
the same firmware image to both OTA app slots, and clears otadata so the
bootloader reselects a valid slot on the next boot.
"""

from __future__ import annotations

import argparse
import csv
import json
import os
import subprocess
import sys
from pathlib import Path


def parse_int(value: str) -> int:
    return int(str(value).strip(), 0)


def fmt(value: int) -> str:
    return f"0x{value:x}"


def run(cmd: list[str], cwd: Path, dry_run: bool = False) -> None:
    print("+ " + " ".join(str(part) for part in cmd))
    if not dry_run:
        subprocess.run(cmd, cwd=str(cwd), check=True)


def project_config(project_dir: Path) -> dict[str, dict[str, object]]:
    proc = subprocess.run(
        ["pio", "project", "config", "--json-output"],
        cwd=str(project_dir),
        check=True,
        text=True,
        stdout=subprocess.PIPE,
    )
    result: dict[str, dict[str, object]] = {}
    for section, values in json.loads(proc.stdout):
        result[section] = {key: value for key, value in values}
    return result


def first_env(config: dict[str, dict[str, object]]) -> str:
    default_envs = config.get("platformio", {}).get("default_envs")
    if isinstance(default_envs, list) and default_envs:
        return str(default_envs[0])
    for section in config:
        if section.startswith("env:"):
            return section.split(":", 1)[1]
    raise SystemExit("No PlatformIO env found; pass --environment explicitly")


def env_option(config: dict[str, dict[str, object]], env_name: str, key: str, default: object = None) -> object:
    return config.get(f"env:{env_name}", {}).get(key, default)


def partition_csv_path(project_dir: Path, config: dict[str, dict[str, object]], env_name: str) -> Path:
    # PlatformIO generates partitions.bin with gen_esp32part.py; this script
    # keeps the human-readable CSV as the source for OTA slot offsets.
    configured = env_option(config, env_name, "board_build.partitions")
    if configured:
        path = Path(str(configured))
        return path if path.is_absolute() else (project_dir / path).resolve()
    generated = project_dir / ".pio" / "build" / env_name / "partitions.csv"
    if generated.exists():
        return generated
    raise SystemExit("Partition CSV not found; set board_build.partitions or build once first")


def parse_partitions(path: Path) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    with path.open(newline="") as fh:
        for raw in csv.reader(line for line in fh if line.strip() and not line.lstrip().startswith("#")):
            if len(raw) < 5:
                continue
            name, ptype, subtype, offset, size = [item.strip() for item in raw[:5]]
            rows.append(
                {
                    "name": name,
                    "type": ptype,
                    "subtype": subtype,
                    "offset": parse_int(offset),
                    "size": parse_int(size),
                }
            )
    return rows


def find_partition(rows: list[dict[str, object]], *, ptype: str | None = None, subtype: str | None = None, name: str | None = None) -> dict[str, object] | None:
    for row in rows:
        if ptype is not None and row["type"] != ptype:
            continue
        if subtype is not None and row["subtype"] != subtype:
            continue
        if name is not None and row["name"] != name:
            continue
        return row
    return None


def ota_partitions(rows: list[dict[str, object]]) -> list[dict[str, object]]:
    result = [row for row in rows if row["type"] == "app" and str(row["subtype"]).startswith("ota_")]
    result.sort(key=lambda row: int(row["offset"]))
    return result


def find_esptool() -> Path:
    candidates = [
        Path.home() / ".platformio" / "packages" / "tool-esptoolpy" / "esptool.py",
        Path.home() / ".platformio" / "packages" / "tool-esptoolpy@src-5bbe6d77617a811fb7fdb314e184ccc2" / "esptool.py",
    ]
    for candidate in candidates:
        if candidate.exists():
            return candidate
    raise SystemExit("esptool.py not found in ~/.platformio/packages/tool-esptoolpy")


def find_boot_app0() -> Path | None:
    candidate = Path.home() / ".platformio" / "packages" / "framework-arduinoespressif32" / "tools" / "partitions" / "boot_app0.bin"
    return candidate if candidate.exists() else None


def main() -> int:
    parser = argparse.ArgumentParser(description="Recover Esp32Base dual-OTA firmware over serial")
    parser.add_argument("-d", "--project-dir", default=".", help="PlatformIO project directory")
    parser.add_argument("-e", "--environment", help="PlatformIO environment")
    parser.add_argument("-p", "--port", help="Serial port; defaults to upload_port when it is a serial device")
    parser.add_argument("-b", "--baud", default="460800", help="Serial baud rate")
    parser.add_argument("--chip", default="esp32", help="esptool chip argument")
    parser.add_argument("--firmware", help="Firmware .bin path; defaults to .pio/build/<env>/firmware.bin")
    parser.add_argument("--bootloader-offset", default="0x1000", help="Bootloader flash offset")
    parser.add_argument("--partition-offset", default="0x8000", help="Partition table flash offset")
    parser.add_argument("--boot-app0-offset", help="boot_app0 offset; defaults to ota_0 offset minus 0x2000")
    parser.add_argument("--no-build", action="store_true", help="Use existing build artifacts")
    parser.add_argument("--no-bootloader", action="store_true", help="Do not write bootloader.bin")
    parser.add_argument("--no-partitions", action="store_true", help="Do not write partitions.bin")
    parser.add_argument("--no-boot-app0", action="store_true", help="Do not write boot_app0.bin")
    parser.add_argument("--write-both-ota", action=argparse.BooleanOptionalAction, default=True, help="Write firmware to all OTA app slots")
    parser.add_argument("--clear-otadata", action=argparse.BooleanOptionalAction, default=True, help="Erase data/ota partition after writing")
    parser.add_argument("--dry-run", action="store_true", help="Print esptool commands without executing them")
    args = parser.parse_args()

    project_dir = Path(args.project_dir).expanduser().resolve()
    config = project_config(project_dir)
    env_name = args.environment or first_env(config)
    build_dir = project_dir / ".pio" / "build" / env_name
    if not args.no_build:
        run(["pio", "run", "-e", env_name], project_dir, args.dry_run)

    csv_path = partition_csv_path(project_dir, config, env_name)
    rows = parse_partitions(csv_path)
    ota_slots = ota_partitions(rows)
    if not ota_slots:
        raise SystemExit(f"No OTA app slots found in {csv_path}")
    otadata = find_partition(rows, ptype="data", subtype="ota")
    if args.clear_otadata and not otadata:
        raise SystemExit(f"No data/ota partition found in {csv_path}; cannot clear otadata")

    firmware = Path(args.firmware).expanduser().resolve() if args.firmware else build_dir / "firmware.bin"
    bootloader = build_dir / "bootloader.bin"
    partitions_bin = build_dir / "partitions.bin"
    boot_app0 = find_boot_app0()
    required = [firmware]
    if not args.no_bootloader:
        required.append(bootloader)
    if not args.no_partitions:
        required.append(partitions_bin)
    if not args.no_boot_app0 and boot_app0:
        required.append(boot_app0)
    missing = [str(path) for path in required if not path.exists()]
    if missing:
        raise SystemExit("Missing build artifact(s): " + ", ".join(missing))

    upload_port = env_option(config, env_name, "upload_port")
    port = args.port or (str(upload_port) if upload_port and str(upload_port).startswith(("/dev/", "COM", "com")) else None)
    if not port:
        raise SystemExit("Missing serial port; pass --port /dev/ttyUSB0")

    esptool = find_esptool()
    write_args: list[str] = []
    if not args.no_bootloader:
        write_args += [args.bootloader_offset, str(bootloader)]
    if not args.no_partitions:
        write_args += [args.partition_offset, str(partitions_bin)]
    if not args.no_boot_app0 and boot_app0:
        boot_app0_offset = parse_int(args.boot_app0_offset) if args.boot_app0_offset else int(ota_slots[0]["offset"]) - 0x2000
        write_args += [fmt(boot_app0_offset), str(boot_app0)]

    selected_ota_slots = ota_slots if args.write_both_ota else [ota_slots[0]]
    for slot in selected_ota_slots:
        write_args += [fmt(int(slot["offset"])), str(firmware)]

    print(f"esp32base_serial_recover_ota environment={env_name} partition_csv={csv_path}")
    print("OTA slots: " + ", ".join(f"{slot['subtype']}@{fmt(int(slot['offset']))}" for slot in ota_slots))
    if args.clear_otadata and otadata:
        print(f"otadata: {fmt(int(otadata['offset']))} size={fmt(int(otadata['size']))}")

    base_cmd = [sys.executable, str(esptool), "--chip", args.chip, "--port", port, "--baud", str(args.baud)]
    run(base_cmd + ["write_flash", "-z"] + write_args, project_dir, args.dry_run)
    if args.clear_otadata and otadata:
        run(base_cmd + ["erase_region", fmt(int(otadata["offset"])), fmt(int(otadata["size"]))], project_dir, args.dry_run)
    print("Recovery flash commands completed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
