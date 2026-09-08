#!/usr/bin/env python3
"""Compile the public MQTT example against the local ESP32 TLS package.

Uses no real credentials and never uploads or connects to a device/Broker.
"""

import hashlib
import json
from pathlib import Path
import subprocess
import sys

from build_tls_toolchain import CACHE, LOCK, ROOT


def write_if_changed(path: Path, data: bytes) -> None:
    if not path.exists() or path.read_bytes() != data:
        path.write_bytes(data)


def main():
    package = CACHE / "package-esp32"
    receipt = json.loads((package / "esp32base-build.json").read_text())
    if receipt["target"] != "esp32" or receipt["source_lock"] != LOCK:
        raise RuntimeError("Package provenance does not match the controlled source lock")
    recipe = ROOT / "scripts/build_tls_toolchain.py"
    if receipt["recipe_sha256"] != hashlib.sha256(recipe.read_bytes()).hexdigest():
        raise RuntimeError("Package was made by a different build recipe; audit and package again")
    for relative, expected in receipt["files_sha256"].items():
        path = (package / relative).resolve()
        if not path.is_relative_to(package.resolve()):
            raise RuntimeError("Invalid package receipt path")
        if hashlib.sha256(path.read_bytes()).hexdigest() != expected:
            raise RuntimeError(f"Package content changed: {relative}")
    probe = CACHE / "probe"
    (probe / "src").mkdir(parents=True, exist_ok=True)
    if (probe / "local_secrets.h").exists():
        raise RuntimeError("Probe must not contain a private local_secrets.h")
    for source, destination in (
        (ROOT / "examples/mqtt_tls/src/main.cpp", probe / "src/main.cpp"),
        (ROOT / "examples/mqtt_tls/local_secrets.example.h", probe / "local_secrets.example.h"),
    ):
        write_if_changed(destination, source.read_bytes())
    for source in (ROOT / "examples/basic/src").glob("deps_*.cpp"):
        write_if_changed(probe / "src" / source.name, source.read_bytes())
    config = (ROOT / "examples/basic/platformio.ini").read_text()
    original_url = f"https://github.com/espressif/arduino-esp32/releases/download/{LOCK['arduino_core']}/esp32-core-{LOCK['arduino_core']}-libs.tar.xz"
    guard = "${esp32base_size_flags.build_flags}\n  -D ESP32BASE_PROFILE=ESP32BASE_PROFILE_IOT"
    if config.count(original_url) != 1 or config.count(guard) != 1:
        raise RuntimeError("Example configuration changed; review the TLS probe")
    config = config.replace("[platformio]\n", f"[platformio]\nsrc_dir = {probe / 'src'}\n")
    config = config.replace("lib_extra_dirs = ../..", f"lib_extra_dirs = {ROOT}")
    config = config.replace("symlink://../..", f"symlink://{ROOT}")
    config = config.replace("../../scripts/", str(ROOT / "scripts") + "/")
    config = config.replace("../../partitions/", str(ROOT / "partitions") + "/")
    config = config.replace(original_url, f"file://{package}")
    config = config.replace(guard,
        "${esp32base_size_flags.build_flags}\n  -include " + str(probe / "require_dates.h")
        + "\n  -D ESP32BASE_PROFILE=ESP32BASE_PROFILE_IOT")
    write_if_changed(probe / "platformio.ini", config.encode())
    write_if_changed(probe / "require_dates.h",
        b"#include <sdkconfig.h>\n#if !defined(CONFIG_MBEDTLS_HAVE_TIME_DATE) || !CONFIG_MBEDTLS_HAVE_TIME_DATE\n"
        b"#error TLS probe requires actual SDK certificate date checks\n#endif\n")
    subprocess.run([sys.executable, str(ROOT / "scripts/pio_arduino.py"), "3", "--tls-toolchain",
                    "run", "-d", str(probe), "-e", "esp32_iot_arduino3"], check=True)
    elf = probe / ".pio/build/arduino3-tls/esp32_iot_arduino3/firmware.elf"
    nm = ROOT / ".piohome/arduino3-tls/packages/toolchain-xtensa-esp-elf/bin/xtensa-esp32-elf-nm"
    symbols = subprocess.check_output([str(nm), str(elf)], text=True)
    for name in ("mbedtls_x509_crt_verify_restartable", "mbedtls_x509_time_gmtime", "mbedtls_x509_time_cmp"):
        if not any(line.endswith(" T " + name) for line in symbols.splitlines()):
            raise RuntimeError(f"TLS verification path missing from firmware: {name}")
    print("MQTT example links the date-aware certificate verifier. No hardware handshake was tested.")


if __name__ == "__main__":
    main()
