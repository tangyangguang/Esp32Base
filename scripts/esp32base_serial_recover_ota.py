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
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


def parse_int(value: str) -> int:
    return int(str(value).strip(), 0)


def parse_size(value: str) -> int:
    text = str(value).strip()
    if not text:
        raise ValueError("empty size")
    suffix = text[-1].upper()
    if suffix in ("K", "M"):
        number = text[:-1].strip()
        if not number:
            raise ValueError(f"invalid size: {value}")
        multiplier = 1024 if suffix == "K" else 1024 * 1024
        return int(number, 0) * multiplier
    return parse_int(text)


def align_up(value: int, alignment: int) -> int:
    return (value + alignment - 1) // alignment * alignment


def fmt(value: int) -> str:
    return f"0x{value:x}"


def run(cmd: list[str], cwd: Path, dry_run: bool = False) -> None:
    print("+ " + " ".join(str(part) for part in cmd), flush=True)
    if not dry_run:
        subprocess.run(cmd, cwd=str(cwd), check=True)


def serial_port_busy_report(port: str) -> str | None:
    if not port.startswith("/dev/"):
        return None
    lsof = shutil.which("lsof")
    if not lsof:
        return None
    proc = subprocess.run([lsof, port], text=True, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    if proc.returncode != 0:
        return None
    lines = [line for line in proc.stdout.splitlines() if line.strip()]
    if len(lines) <= 1:
        return None
    return "\n".join(lines[:8])


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
    next_offset = 0x9000
    with path.open(newline="") as fh:
        for raw in csv.reader(line for line in fh if line.strip() and not line.lstrip().startswith("#")):
            if len(raw) < 5:
                continue
            name, ptype, subtype, offset, size = [item.strip() for item in raw[:5]]
            parsed_size = parse_size(size)
            if offset:
                parsed_offset = parse_int(offset)
            else:
                parsed_offset = align_up(next_offset, 0x10000 if ptype == "app" else 0x1000)
            rows.append(
                {
                    "name": name,
                    "type": ptype,
                    "subtype": subtype,
                    "offset": parsed_offset,
                    "size": parsed_size,
                }
            )
            next_offset = max(next_offset, parsed_offset + parsed_size)
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


def write_erased_image(path: Path, size: int) -> None:
    if size <= 0:
        raise SystemExit(f"Invalid otadata size: {fmt(size)}")
    path.write_bytes(b"\xff" * size)


def ranges_overlap(a_offset: int, a_size: int, b_offset: int, b_size: int) -> bool:
    return max(a_offset, b_offset) < min(a_offset + a_size, b_offset + b_size)


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
    parser.add_argument("--boot-app0-offset", help="boot_app0 offset; defaults to data/ota offset when present")
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
    if not args.dry_run:
        busy = serial_port_busy_report(port)
        if busy:
            raise SystemExit(
                "Serial port appears busy; close pio device monitor or any other serial monitor before recovery.\n"
                + busy
            )

    esptool = find_esptool()
    base_cmd = [sys.executable, str(esptool), "--chip", args.chip, "--port", port, "--baud", str(args.baud)]
    selected_ota_slots = ota_slots if args.write_both_ota else [ota_slots[0]]

    with tempfile.TemporaryDirectory(prefix="esp32base-ota-recover-") as tmp:
        write_args: list[str] = []
        flash_summary: list[str] = []
        flash_notes: list[str] = []

        def add_flash_item(offset: str | int, image: Path, label: str) -> None:
            offset_text = fmt(offset) if isinstance(offset, int) else offset
            write_args.extend([offset_text, str(image)])
            flash_summary.append(f"{label}@{offset_text} <= {image}")

        if not args.no_bootloader:
            add_flash_item(args.bootloader_offset, bootloader, "bootloader")
        if not args.no_partitions:
            add_flash_item(args.partition_offset, partitions_bin, "partition_table")
        if not args.no_boot_app0 and boot_app0:
            if args.boot_app0_offset:
                boot_app0_offset = parse_int(args.boot_app0_offset)
            elif otadata:
                boot_app0_offset = int(otadata["offset"])
            else:
                boot_app0_offset = int(ota_slots[0]["offset"]) - 0x2000
            if args.clear_otadata and otadata and ranges_overlap(
                boot_app0_offset,
                boot_app0.stat().st_size,
                int(otadata["offset"]),
                int(otadata["size"]),
            ):
                flash_notes.append("boot_app0: skipped because it overlaps otadata being cleared")
            else:
                add_flash_item(boot_app0_offset, boot_app0, "boot_app0")
        if args.clear_otadata and otadata:
            erased_otadata = Path(tmp) / "otadata-erased-0xff.bin"
            write_erased_image(erased_otadata, int(otadata["size"]))
            add_flash_item(int(otadata["offset"]), erased_otadata, "otadata_erased")
        for slot in selected_ota_slots:
            add_flash_item(int(slot["offset"]), firmware, str(slot["subtype"]))

        print(f"esp32base_serial_recover_ota environment={env_name} partition_csv={csv_path}")
        print("OTA slots detected: " + ", ".join(f"{slot['subtype']}@{fmt(int(slot['offset']))}" for slot in ota_slots))
        print("OTA slots written: " + ", ".join(f"{slot['subtype']}@{fmt(int(slot['offset']))}" for slot in selected_ota_slots))
        if args.clear_otadata and otadata:
            print(f"otadata: {fmt(int(otadata['offset']))} size={fmt(int(otadata['size']))} clear=write 0xff image in write_flash")
        else:
            print("otadata: clear=skipped; bootloader may still follow the previously selected OTA slot")
        for note in flash_notes:
            print(note)
        print("Flash plan:")
        for item in flash_summary:
            print(f"  - {item}")
        sys.stdout.flush()

        try:
            run(base_cmd + ["write_flash", "-z"] + write_args, project_dir, args.dry_run)
        except subprocess.CalledProcessError as exc:
            print(
                "Recovery write_flash failed; do not trust the device boot result. "
                "If otadata was not cleared, the bootloader may still select the previous OTA slot "
                "(often ota_1 after WebOTA). Close any serial monitor, try --baud 115200 if the serial link is unstable, "
                "and re-run the recovery command from download mode.",
                file=sys.stderr,
            )
            raise exc

    if args.clear_otadata and otadata:
        print("Recovery flash command completed; otadata was cleared in the same write_flash command")
    else:
        print("Recovery flash command completed; otadata was not cleared")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
