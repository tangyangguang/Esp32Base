from __future__ import annotations

import contextlib
import io
import subprocess
import tempfile
from pathlib import Path
from unittest import mock

import esp32base_serial_recover_ota as recover


def write_file(path: Path, content: bytes | str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if isinstance(content, str):
        path.write_text(content, encoding="utf-8")
    else:
        path.write_bytes(content)


def make_project(base: Path, name: str, partition_csv: str) -> Path:
    project = base / name
    write_file(project / "partitions.csv", partition_csv)
    build = project / ".pio" / "build" / "esp32dev"
    write_file(build / "firmware.bin", b"firmware")
    write_file(build / "bootloader.bin", b"bootloader")
    write_file(build / "partitions.bin", b"partitions")
    return project


def make_custom_offset_project(base: Path) -> Path:
    return make_project(
        base,
        "Esp32_CustomOffsets",
        "\n".join(
            [
                "# Name, Type, SubType, Offset, Size, Flags",
                "nvs, data, nvs, 0x9000, 0x10000,",
                "otadata, data, ota, 0x19000, 0x2000,",
                "ota_0, app, ota_0, 0x20000, 0x160000,",
                "ota_1, app, ota_1, 0x180000, 0x160000,",
                "spiffs, data, spiffs, 0x2e0000, 0x110000,",
            ]
        ),
    )


def make_standard_project(base: Path) -> Path:
    return make_project(
        base,
        "Esp32_Standard",
        "\n".join(
            [
                "# Name, Type, SubType, Offset, Size, Flags",
                "nvs, data, nvs, 0x9000, 0x5000,",
                "otadata, data, ota, 0xe000, 0x2000,",
                "ota_0, app, ota_0, 0x10000, 0x180000,",
                "ota_1, app, ota_1, 0x190000, 0x180000,",
                "spiffs, data, spiffs, 0x310000, 0xE0000,",
            ]
        ),
    )


def make_unit_offset_project(base: Path) -> Path:
    return make_project(
        base,
        "Esp32_UnitOffsets",
        "\n".join(
            [
                "# Name, Type, SubType, Offset, Size, Flags",
                "nvs, data, nvs, 0x9000, 20K,",
                "otadata, data, ota, , 8K,",
                "ota_0, app, ota_0, , 1408K,",
                "ota_1, app, ota_1, , 1408K,",
                "spiffs, data, spiffs, , 1M,",
            ]
        ),
    )


def fake_config(project_dir: Path) -> dict[str, dict[str, object]]:
    return {
        "platformio": {"default_envs": ["esp32dev"]},
        "env:esp32dev": {
            "board_build.partitions": "partitions.csv",
            "upload_port": "/dev/ttyUSB0",
        },
    }


def dry_run(project: Path) -> tuple[int, str]:
    fake_esptool = project / "esptool.py"
    fake_boot_app0 = project / "boot_app0.bin"
    write_file(fake_esptool, b"")
    write_file(fake_boot_app0, b"boot_app0")

    output = io.StringIO()
    argv = [
        "esp32base_serial_recover_ota.py",
        "-d",
        str(project),
        "--dry-run",
        "--no-build",
    ]
    with mock.patch.object(recover, "project_config", side_effect=fake_config), mock.patch.object(
        recover, "find_esptool", return_value=fake_esptool
    ), mock.patch.object(recover, "find_boot_app0", return_value=fake_boot_app0), mock.patch.object(
        recover.sys, "argv", argv
    ), contextlib.redirect_stdout(output):
        rc = recover.main()
    return rc, output.getvalue()


def write_flash_lines(text: str) -> list[str]:
    return [line for line in text.splitlines() if line.startswith("+ ") and " write_flash " in line]


def command_offsets(command: str) -> list[str]:
    return [part for part in command.split() if part.startswith("0x")]


def check_custom_offset_recovery(tmp: Path, errors: list[str]) -> None:
    rc, text = dry_run(make_custom_offset_project(tmp))
    commands = write_flash_lines(text)

    if rc != 0:
        errors.append(f"expected custom-offset rc=0, got {rc}")
    if len(commands) != 1:
        errors.append(f"expected exactly one custom-offset write_flash command, got {len(commands)}")
    else:
        offsets = command_offsets(commands[0])
        if not {"0x19000", "0x20000", "0x180000"}.issubset(set(offsets)):
            errors.append("custom-offset write_flash command must include otadata, ota_0, and ota_1 offsets")
        if "0x1e000" in offsets:
            errors.append("custom-offset recovery must not write boot_app0 into the ota_0-adjacent gap")
    if " erase_region " in text:
        errors.append("custom-offset dry-run must not emit a second erase_region command")


def check_standard_overlap(tmp: Path, errors: list[str]) -> None:
    rc, text = dry_run(make_standard_project(tmp))
    commands = write_flash_lines(text)

    if rc != 0:
        errors.append(f"expected standard rc=0, got {rc}")
    if len(commands) != 1:
        errors.append(f"expected exactly one standard write_flash command, got {len(commands)}")
    else:
        offsets = command_offsets(commands[0])
        if offsets.count("0xe000") != 1:
            errors.append("standard write_flash command must clear otadata once and skip overlapping boot_app0")


def check_unit_and_blank_offsets(tmp: Path, errors: list[str]) -> None:
    try:
        rc, text = dry_run(make_unit_offset_project(tmp))
    except Exception as exc:
        errors.append(f"unit/blank offset dry-run must not raise: {exc}")
        return
    commands = write_flash_lines(text)

    if rc != 0:
        errors.append(f"expected unit/blank rc=0, got {rc}")
    if len(commands) != 1:
        errors.append(f"expected exactly one unit/blank write_flash command, got {len(commands)}")
    else:
        offsets = command_offsets(commands[0])
        if not {"0xe000", "0x10000", "0x170000"}.issubset(set(offsets)):
            errors.append("unit/blank write_flash command must include resolved otadata, ota_0, and ota_1 offsets")


def check_serial_busy_report(errors: list[str]) -> None:
    lsof_output = "\n".join(
        [
            "COMMAND   PID USER   FD   TYPE DEVICE SIZE/OFF NODE NAME",
            "Python  36258  tyg    3u   CHR   9,11     0t53 1243 /dev/cu.usbserial-130",
        ]
    )
    completed = subprocess.CompletedProcess(["lsof", "/dev/cu.usbserial-130"], 0, stdout=lsof_output)
    with mock.patch.object(recover.shutil, "which", return_value="/usr/sbin/lsof"), mock.patch.object(
        recover.subprocess, "run", return_value=completed
    ):
        report = recover.serial_port_busy_report("/dev/cu.usbserial-130")
    if not report or "Python" not in report or "usbserial-130" not in report:
        errors.append("serial busy report must show the process holding the recovery port")


def main() -> int:
    errors: list[str] = []
    with tempfile.TemporaryDirectory() as tmp:
        base = Path(tmp)
        check_custom_offset_recovery(base, errors)
        check_standard_overlap(base, errors)
        check_unit_and_blank_offsets(base, errors)
        check_serial_busy_report(errors)

    if errors:
        for error in errors:
            print(f"ERROR: {error}")
        return 1

    print("Serial OTA recovery dry-run checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
