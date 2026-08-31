#!/usr/bin/env python3
from pathlib import Path
import os
import shutil
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
CORE_DIR = ROOT / ".piohome" / "arduino3"
BASIC_EXAMPLE = ROOT / "examples" / "basic"
ARDUINO3_ENVS = ("esp32_local_arduino3", "esp32_minimal_arduino3")
REQUIRED_PATHS = (
    Path("framework-arduinoespressif32") / "package.json",
    Path("framework-arduinoespressif32") / "cores" / "esp32" / "Arduino.h",
    Path("framework-arduinoespressif32") / "libraries" / "WiFi" / "src" / "WiFi.h",
    Path("framework-arduinoespressif32") / "libraries" / "Network" / "src" / "NetworkClient.h",
    Path("framework-arduinoespressif32") / "libraries" / "Hash" / "src" / "PBKDF2_HMACBuilder.h",
    Path("framework-arduinoespressif32") / "tools" / "pioarduino-build.py",
    Path("framework-arduinoespressif32-libs") / "package.json",
    Path("framework-arduinoespressif32-libs") / "tools.json",
    Path("framework-arduinoespressif32-libs") / "esp32" / "lib",
)
REQUIRED_TOOL_PATHS = (
    Path("tool-esptoolpy") / "package.json",
    Path("toolchain-xtensa-esp-elf") / "bin" / "xtensa-esp32-elf-g++",
)


def run(command: list[str]) -> None:
    environment = os.environ.copy()
    environment["PLATFORMIO_CORE_DIR"] = str(CORE_DIR)
    print(f"PLATFORMIO_CORE_DIR={CORE_DIR}", flush=True)
    print("+ " + " ".join(command), flush=True)
    subprocess.run(command, cwd=ROOT, env=environment, check=True)


def main() -> int:
    pio = shutil.which("pio")
    if not pio:
        print("PlatformIO executable 'pio' was not found in PATH", file=sys.stderr)
        return 1

    CORE_DIR.mkdir(parents=True, exist_ok=True)
    for env in ARDUINO3_ENVS:
        run([pio, "pkg", "install", "-d", str(BASIC_EXAMPLE), "-e", env, "--no-save"])

    packages_dir = CORE_DIR / "packages"
    tools_dir = CORE_DIR / "tools"
    errors = []
    for root, required_paths in (
        (packages_dir, REQUIRED_PATHS),
        (tools_dir, REQUIRED_TOOL_PATHS),
    ):
        for required in required_paths:
            path = root / required
            if not path.exists():
                errors.append(str(path))

    if errors:
        print("Missing required pioarduino Core 3.x package paths:", file=sys.stderr)
        for error in errors:
            print(f"  {error}", file=sys.stderr)
        return 1

    print(f"pioarduino Core 3.x packages are ready in {CORE_DIR}")
    print("Run Core 3.x builds through: python3 scripts/pio_arduino3.py run ...")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
