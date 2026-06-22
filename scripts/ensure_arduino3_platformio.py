#!/usr/bin/env python3
from pathlib import Path
import shutil
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
BASIC_EXAMPLE = ROOT / "examples" / "basic"
ARDUINO3_ENVS = ("esp32_full_arduino3", "esp32_core_arduino3")
REQUIRED_PATHS = (
    Path("framework-arduinoespressif32") / "package.json",
    Path("framework-arduinoespressif32") / "cores" / "esp32" / "Arduino.h",
    Path("framework-arduinoespressif32") / "libraries" / "WiFi" / "src" / "WiFi.h",
    Path("framework-arduinoespressif32") / "tools" / "pioarduino-build.py",
    Path("framework-arduinoespressif32-libs") / "package.json",
    Path("framework-arduinoespressif32-libs") / "tools.json",
    Path("framework-arduinoespressif32-libs") / "esp32" / "lib",
    Path("tool-esptoolpy") / "package.json",
    Path("toolchain-xtensa-esp-elf") / "bin" / "xtensa-esp32-elf-g++",
)


def run(command: list[str]) -> None:
    print("+ " + " ".join(command), flush=True)
    subprocess.run(command, cwd=ROOT, check=True)


def main() -> int:
    pio = shutil.which("pio")
    if not pio:
        print("PlatformIO executable 'pio' was not found in PATH", file=sys.stderr)
        return 1

    for env in ARDUINO3_ENVS:
        run([pio, "pkg", "install", "-d", str(BASIC_EXAMPLE), "-e", env, "--no-save"])

    packages_dir = Path.home() / ".platformio" / "packages"
    errors = []
    for required in REQUIRED_PATHS:
        path = packages_dir / required
        if not path.exists():
            errors.append(str(path))

    if errors:
        print("Missing required pioarduino Core 3.x package paths:", file=sys.stderr)
        for error in errors:
            print(f"  {error}", file=sys.stderr)
        return 1

    print("pioarduino Core 3.x packages are ready")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
