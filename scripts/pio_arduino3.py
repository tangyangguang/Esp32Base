#!/usr/bin/env python3
"""Run PlatformIO with an isolated repository-local pioarduino Core 3.x home."""

from __future__ import annotations

import os
from pathlib import Path
import shutil
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
CORE_DIR = ROOT / ".piohome" / "arduino3"


def main() -> int:
    pio = shutil.which("pio")
    if not pio:
        print("PlatformIO executable 'pio' was not found in PATH", file=sys.stderr)
        return 1
    if len(sys.argv) < 2:
        print("usage: python3 scripts/pio_arduino3.py <pio arguments...>", file=sys.stderr)
        return 2

    CORE_DIR.mkdir(parents=True, exist_ok=True)
    environment = os.environ.copy()
    environment["PLATFORMIO_CORE_DIR"] = str(CORE_DIR)
    command = [pio, *sys.argv[1:]]
    print(f"PLATFORMIO_CORE_DIR={CORE_DIR}", flush=True)
    print("+ " + " ".join(command), flush=True)
    return subprocess.run(command, cwd=ROOT, env=environment, check=False).returncode


if __name__ == "__main__":
    raise SystemExit(main())
