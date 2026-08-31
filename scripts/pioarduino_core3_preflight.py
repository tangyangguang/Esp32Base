from pathlib import Path
import os
import shutil
import subprocess

Import("env")

PLATFORM = env.PioPlatform()
ORIGINAL_GET_PACKAGE_DIR = PLATFORM.get_package_dir

REQUIRED_PACKAGE_PATHS = {
    "framework-arduinoespressif32": (
        Path("package.json"),
        Path("tools") / "pioarduino-build.py",
        Path("cores") / "esp32" / "Arduino.h",
        Path("libraries") / "Network" / "src" / "NetworkClient.h",
        Path("libraries") / "Hash" / "src" / "PBKDF2_HMACBuilder.h",
    ),
    "framework-arduinoespressif32-libs": (
        Path("package.json"),
        Path("tools.json"),
        Path("esp32") / "lib",
    ),
    "tool-esptoolpy": (
        Path("package.json"),
    ),
    "toolchain-xtensa-esp-elf": (
        Path("bin") / "xtensa-esp32-elf-g++",
    ),
}


def packages_root() -> Path:
    configured = env.subst("$PIOPACKAGES_DIR")
    if configured and "$" not in configured:
        return Path(configured)
    return Path.home() / ".platformio" / "packages"


def package_dir(name: str) -> Path | None:
    path = ORIGINAL_GET_PACKAGE_DIR(name)
    if path:
        return Path(path)
    fallback = packages_root() / name
    return fallback if fallback.exists() else None


def patched_get_package_dir(name: str):
    path = ORIGINAL_GET_PACKAGE_DIR(name)
    if path:
        return path
    fallback = packages_root() / name
    if name in REQUIRED_PACKAGE_PATHS and fallback.exists():
        return str(fallback)
    return path


def missing_paths() -> list[str]:
    missing = []
    for package, required_paths in REQUIRED_PACKAGE_PATHS.items():
        root = package_dir(package)
        if not root:
            missing.append(package)
            continue
        for relative in required_paths:
            path = root / relative
            if not path.exists():
                missing.append(str(path))
    return missing


missing = missing_paths()
if missing:
    pio = shutil.which("pio")
    if pio and os.environ.get("ESP32BASE_PIOARDUINO_PREFLIGHT_INSTALL") != "1":
        install_env = os.environ.copy()
        install_env["ESP32BASE_PIOARDUINO_PREFLIGHT_INSTALL"] = "1"
        subprocess.run(
            [
                pio,
                "pkg",
                "install",
                "-d",
                env.subst("$PROJECT_DIR"),
                "-e",
                env.subst("$PIOENV"),
                "--no-save",
            ],
            check=True,
            env=install_env,
        )
        missing = missing_paths()

if missing:
    raise RuntimeError(
        "pioarduino Core 3.x package preflight failed; run "
        "`python3 scripts/ensure_arduino_platformio.py` from the repository root, then "
        "use `python3 scripts/pio_arduino.py 3 ...`. "
        "Missing: " + ", ".join(missing)
    )

PLATFORM.get_package_dir = patched_get_package_dir
