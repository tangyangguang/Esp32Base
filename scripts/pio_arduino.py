#!/usr/bin/env python3
"""Run PlatformIO in the repository-local Arduino Core 2.x or 3.x home."""

from __future__ import annotations

import os
from pathlib import Path
import shutil
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]


def option_values(arguments: list[str], names: tuple[str, ...]) -> list[str]:
    values: list[str] = []
    index = 0
    while index < len(arguments):
        argument = arguments[index]
        if argument in names and index + 1 < len(arguments):
            values.append(arguments[index + 1])
            index += 2
            continue
        for name in names:
            if argument.startswith(name + "="):
                values.append(argument.split("=", 1)[1])
                break
        index += 1
    return values


def selected_environments(arguments: list[str]) -> list[str]:
    return option_values(arguments, ("-e", "--environment"))


def project_dir(arguments: list[str]) -> Path:
    selected = option_values(arguments, ("-d", "--project-dir"))
    path = Path(selected[-1]) if selected else Path.cwd()
    if not path.is_absolute():
        path = Path.cwd() / path
    return path.resolve()


def main() -> int:
    if len(sys.argv) < 3 or sys.argv[1] not in ("2", "3"):
        print(
            "usage: python3 scripts/pio_arduino.py <2|3> <pio arguments...>",
            file=sys.stderr,
        )
        return 2

    major = sys.argv[1]
    arguments = sys.argv[2:]
    selected = selected_environments(arguments)
    mismatched = [
        name
        for name in selected
        if (major == "3") != name.endswith("_arduino3")
    ]
    if mismatched:
        print(
            f"Arduino Core {major}.x wrapper received incompatible env(s): "
            + ", ".join(mismatched),
            file=sys.stderr,
        )
        return 2

    pio = shutil.which("pio")
    if not pio:
        print("PlatformIO executable 'pio' was not found in PATH", file=sys.stderr)
        return 1

    core_dir = ROOT / ".piohome" / f"arduino{major}"
    project = project_dir(arguments)
    build_dir = project / ".pio" / "build" / f"arduino{major}"
    libdeps_dir = project / ".pio" / "libdeps" / f"arduino{major}"
    core_dir.mkdir(parents=True, exist_ok=True)
    environment = os.environ.copy()
    environment["PLATFORMIO_CORE_DIR"] = str(core_dir)
    environment["PLATFORMIO_BUILD_DIR"] = str(build_dir)
    environment["PLATFORMIO_LIBDEPS_DIR"] = str(libdeps_dir)
    command = [pio, *arguments]
    print(f"PLATFORMIO_CORE_DIR={core_dir}", flush=True)
    print(f"PLATFORMIO_BUILD_DIR={build_dir}", flush=True)
    print(f"PLATFORMIO_LIBDEPS_DIR={libdeps_dir}", flush=True)
    print("+ " + " ".join(command), flush=True)
    return subprocess.run(command, env=environment, check=False).returncode


if __name__ == "__main__":
    raise SystemExit(main())
