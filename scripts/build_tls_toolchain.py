#!/usr/bin/env python3
"""Rebuild the pinned Arduino Core 3 TLS-capable libraries in an isolated cache.

Never installs the resulting package into PlatformIO or touches a device.
"""

from __future__ import annotations

import argparse
import fcntl
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tarfile
import urllib.request


ROOT = Path(__file__).resolve().parents[1]
LOCK = json.loads((ROOT / "scripts/tls_toolchain_core3.json").read_text())
CACHE = ROOT / ".cache/tls-toolchain"
BUILDER = CACHE / "builder"
BASELINE = ROOT / ".piohome/arduino3/packages/framework-arduinoespressif32-libs"
DATE_CONFIG = "\n# Esp32Base: validate certificate notBefore/notAfter.\nCONFIG_MBEDTLS_HAVE_TIME_DATE=y\n"
CCACHE_BIN = CACHE / "ccache-tool/ccache-4.12.1-darwin/ccache"


def config_changes(target: str) -> dict[str, str]:
    return {**LOCK["config_changes"], **LOCK["target_config_changes"].get(target, {})}


def target_config_addition(target: str) -> bytes:
    changes = LOCK["target_config_changes"].get(target, {})
    if not changes:
        return b""
    lines = ["", "# Esp32Base: isolate MQTT from the default application core."]
    for key, value in changes.items():
        lines.append(f"# {key} is not set" if value == "n" else f"{key}={value}")
    return ("\n".join(lines) + "\n").encode()


def capture(command: list[str], cwd: Path) -> str:
    return subprocess.check_output(command, cwd=cwd, text=True).strip()


def run(command: list[str], cwd: Path, environment: dict[str, str]) -> None:
    print("+ " + " ".join(command), flush=True)
    subprocess.run(command, cwd=cwd, env=environment, check=True)


def builder_cmake(original: bytes) -> bytes:
    """ESP32's memory exporter consumes only spi_flash and the generated header.

    Other targets retain upstream dependencies (S3 also exports linker files).
    Firmware sources, compiler flags and the exporter itself remain unchanged.
    """
    text = original.decode()
    anchor = 'add_custom_command(\n\tOUTPUT "mem_variant"'
    if text.count(anchor) != 1:
        raise RuntimeError("Unexpected upstream memory variant target")
    text = text.replace(anchor,
        'set(esp32base_mem_variant_dependency ${elf})\n'
        'if(IDF_TARGET STREQUAL "esp32")\n'
        '    set(esp32base_mem_variant_dependency __idf_spi_flash)\n'
        'endif()\n\n' + anchor)
    position = text.index('OUTPUT "mem_variant"')
    head, tail = text[:position], text[position:]
    if '\tDEPENDS ${elf}' not in tail:
        raise RuntimeError("Unexpected upstream memory variant dependency")
    return (head + tail.replace('\tDEPENDS ${elf}', '\tDEPENDS ${esp32base_mem_variant_dependency}', 1)).encode()


def verify_baseline(target: str) -> None:
    package = json.loads((BASELINE / "package.json").read_text())
    if package["version"] != LOCK["framework_libraries"]:
        raise RuntimeError("Unexpected framework libraries version; refusing to rebuild")
    for name, expected in LOCK["baseline_sha256"][target].items():
        actual = hashlib.sha256((BASELINE / target / name).read_bytes()).hexdigest()
        if actual != expected:
            raise RuntimeError(f"Baseline hash mismatch: {target}/{name}")


def verify_source(relative: str, commit: str) -> None:
    path = CACHE / relative
    if capture(["git", "rev-parse", "HEAD"], path) != commit:
        raise RuntimeError(f"Source revision mismatch: {relative}")
    # Build output is untracked. Tracked changes are forbidden except our exact
    # configuration addition; no reset/checkout may erase unexpected changes.
    changed = capture(["git", "diff", "HEAD", "--name-only", "--ignore-submodules=all"], path).splitlines()
    allowed = (["configs/defconfig.common", "CMakeLists.txt"] +
               [f"configs/defconfig.{target}" for target in LOCK["target_config_changes"]]
               if relative == "builder" else [])
    if any(name not in allowed for name in changed):
        raise RuntimeError(f"Unexpected source edits: {relative}: {changed}")
    if relative == "builder":
        exports = json.loads((path / "configs/builds.json").read_text())["mem_variants_files"]
        esp32_exports = [(item["file"], item["src"]) for item in exports if "esp32" in item["targets"]]
        if esp32_exports != [("libspi_flash.a", "build/esp-idf/spi_flash/libspi_flash.a")]:
            raise RuntimeError("ESP32 memory variant exports changed; review the narrowed build dependency")
        original = subprocess.check_output(["git", "show", "HEAD:configs/defconfig.common"], cwd=path)
        actual = (path / "configs/defconfig.common").read_bytes()
        if actual not in (original, original + DATE_CONFIG.encode()):
            raise RuntimeError("Unexpected builder configuration edits")
        for target in LOCK["target_config_changes"]:
            name = f"configs/defconfig.{target}"
            original_target = subprocess.check_output(["git", "show", f"HEAD:{name}"], cwd=path)
            if (path / name).read_bytes() not in (original_target, original_target + target_config_addition(target)):
                raise RuntimeError(f"Unexpected builder target configuration edits: {target}")
        original_cmake = subprocess.check_output(["git", "show", "HEAD:CMakeLists.txt"], cwd=path)
        if (path / "CMakeLists.txt").read_bytes() not in (original_cmake, builder_cmake(original_cmake)):
            raise RuntimeError("Unexpected builder CMake edits")


def prepare(target: str, environment: dict[str, str]) -> None:
    if sys.platform == "darwin" and not CCACHE_BIN.exists():
        source = LOCK["ccache_darwin"]
        archive = CACHE / "ccache-4.12.1-darwin.tar.gz"
        if not archive.exists():
            with urllib.request.urlopen(source["url"], timeout=60) as response:
                archive.write_bytes(response.read())
        if hashlib.sha256(archive.read_bytes()).hexdigest() != source["sha256"]:
            raise RuntimeError("Compiler cache download checksum mismatch")
        with tarfile.open(archive) as bundle:
            bundle.extractall(CACHE / "ccache-tool", filter="data")
    for relative, source in LOCK["sources"].items():
        path = CACHE / relative
        if not path.exists():
            path.mkdir(parents=True)
            run(["git", "init", "."], path, environment)
            run(["git", "remote", "add", "origin", source["url"]], path, environment)
            run(["git", "fetch", "--depth", "1", "origin", source["commit"]], path, environment)
            run(["git", "checkout", "--detach", "FETCH_HEAD"], path, environment)
        verify_source(relative, source["commit"])
    run(["git", "submodule", "update", "--init", "--recursive", "--depth", "1", "--jobs", "4"],
        CACHE / "esp-idf", environment)
    run(["./install.sh", target], CACHE / "esp-idf", environment)


def configuration(path: Path) -> dict[str, str]:
    result = {}
    for line in path.read_text().splitlines():
        if line.startswith("CONFIG_") and "=" in line:
            key, value = line.split("=", 1)
            result[key] = value
        elif line.startswith("# CONFIG_") and line.endswith(" is not set"):
            result[line[2:-11]] = "n"
    return result


def audit(target: str) -> None:
    output = BUILDER / "out/tools/esp32-arduino-libs" / target
    before = configuration(BASELINE / target / "sdkconfig")
    after = configuration(output / "sdkconfig")
    delta = {key: [before.get(key), after.get(key)] for key in before.keys() | after.keys()
             if before.get(key) != after.get(key)}
    expected = {key: [before.get(key), value] for key, value in config_changes(target).items()}
    if delta != expected:
        raise RuntimeError("Unexpected sdkconfig changes: " + json.dumps(delta, sort_keys=True))
    # The upstream lock is also a contract: building today must not silently
    # select newer camera/Matter/network dependencies from floating manifests.
    if (output / "dependencies.lock").read_bytes() != (BASELINE / target / "dependencies.lock").read_bytes():
        raise RuntimeError("Component dependency lock changed")
    print(f"Configuration and dependency audit passed: {output}")
    print("This is not binary, firmware, TLS handshake or hardware acceptance.")


def audit_binary(target: str) -> None:
    output = BUILDER / "out/tools/esp32-arduino-libs" / target
    nm_name = "riscv32-esp-elf-nm" if target == "esp32c3" else f"xtensa-{target}-elf-nm"
    candidates = list((CACHE / "idf-tools/tools").glob(f"*/*/*/bin/{nm_name}"))
    if len(candidates) != 1:
        raise RuntimeError(f"Expected one pinned binary inspection tool: {nm_name}")
    symbols = capture([str(candidates[0]), "-A", "-g", str(output / "lib/libmbedx509.a")], BUILDER)
    # With date checking disabled, the upstream past/future functions still
    # exist but return zero. Require the compiled UTC conversion path as well.
    for symbol_type, name in (("T", "mbedtls_x509_time_gmtime"), ("U", "mbedtls_platform_gmtime_r")):
        if not re.search(rf"\b{symbol_type}\s+{name}$", symbols, re.MULTILINE):
            raise RuntimeError(f"Missing compiled certificate date path: {name}")
    # A new helper alone is insufficient: the certificate-chain verifier itself
    # must call the time conversion and comparison paths.
    for name in ("mbedtls_x509_time_gmtime", "mbedtls_x509_time_cmp"):
        if not re.search(rf"x509_crt\.c\.(?:obj|o):[^\n]*\bU\s+{name}$", symbols, re.MULTILINE):
            raise RuntimeError(f"Certificate-chain verifier does not use {name}")
    headers = list(output.glob("*/include/sdkconfig.h"))
    expected_headers = {path.relative_to(BASELINE / target)
                        for path in (BASELINE / target).glob("*/include/sdkconfig.h")}
    if not expected_headers or {path.relative_to(output) for path in headers} != expected_headers:
        raise RuntimeError("Memory variant headers do not match the original package")
    for header in headers:
        text = header.read_text()
        for key, value in config_changes(target).items():
            definition = re.search(rf"^#define {re.escape(key)}(?: (.+))?$", text, re.MULTILINE)
            if value == "n":
                valid = definition is None
            else:
                valid = definition is not None and definition.group(1) == ("1" if value == "y" else value)
            if not valid:
                raise RuntimeError(f"Controlled configuration mismatch in {header}: {key}")
    print(f"Compiled date path and {len(headers)} variant headers passed; no device handshake was performed.")


def package(target: str) -> None:
    output = BUILDER / "out/tools/esp32-arduino-libs"
    destination = CACHE / f"package-{target}"
    if destination.exists():
        raise RuntimeError(f"Package already exists; preserve/reuse it before generating another: {destination}")
    destination.mkdir()
    shutil.copytree(output / target, destination / target)
    shutil.copyfile(BASELINE / "tools.json", destination / "tools.json")
    shutil.copyfile(output / "versions.txt", destination / "versions.txt")
    metadata = json.loads((BASELINE / "package.json").read_text())
    metadata["version"] = LOCK["framework_libraries"] + ".esp32base.tls2"
    metadata["description"] = f"Esp32Base controlled Arduino {LOCK['arduino_core']} libraries; target {target}; certificate dates enabled"
    (destination / "package.json").write_text(json.dumps(metadata, indent=2) + "\n")
    provenance = {"target": target, "source_lock": LOCK,
                  "recipe_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                  "files_sha256": {}}
    for path in sorted(destination.rglob("*")):
        if path.is_file():
            provenance["files_sha256"][str(path.relative_to(destination))] = hashlib.sha256(path.read_bytes()).hexdigest()
    (destination / "esp32base-build.json").write_text(json.dumps(provenance, indent=2) + "\n")
    print(f"Local framework package: {destination}")
    print("This package contains only the selected target. No upload or publication was performed.")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("prepare", "build", "audit", "package"))
    parser.add_argument("--target", choices=tuple(LOCK["baseline_sha256"]), default="esp32")
    args = parser.parse_args()
    verify_baseline(args.target)
    CACHE.mkdir(parents=True, exist_ok=True)
    # All targets share upstream build/out. A second invocation must not erase
    # another build's output, and must not queue a surprise full rebuild.
    build_lock = (CACHE / "build.lock").open("a")
    try:
        fcntl.flock(build_lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except BlockingIOError:
        raise RuntimeError("Another TLS toolchain operation is already running") from None
    environment = os.environ.copy()
    environment.update(IDF_PATH=str(CACHE / "esp-idf"), IDF_TOOLS_PATH=str(CACHE / "idf-tools"))
    environment.update(CCACHE_DIR=str(CACHE / "ccache"), CCACHE_MAXSIZE="2G")
    if sys.platform == "darwin" and CCACHE_BIN.exists():
        if hashlib.sha256(CCACHE_BIN.read_bytes()).hexdigest() != LOCK["ccache_darwin"]["binary_sha256"]:
            raise RuntimeError("Compiler cache executable checksum mismatch")
        environment["PATH"] = str(CCACHE_BIN.parent) + os.pathsep + environment.get("PATH", "")
    if args.action == "prepare":
        prepare(args.target, environment)
        return 0
    for relative, source in LOCK["sources"].items():
        verify_source(relative, source["commit"])
    submodules = capture(["git", "submodule", "status", "--recursive"], CACHE / "esp-idf")
    if any(line.startswith(("-", "+", "U")) for line in submodules.splitlines()):
        raise RuntimeError("ESP-IDF submodules are missing or differ from the pinned revision")
    if subprocess.run(["git", "diff", "--quiet", "HEAD", "--ignore-submodules=none"],
                      cwd=CACHE / "esp-idf").returncode:
        raise RuntimeError("ESP-IDF or its submodules contain source edits")
    if args.action in ("audit", "package"):
        audit(args.target)
        audit_binary(args.target)
        if args.action == "package":
            package(args.target)
        return 0
    for executable in ("git", "cmake", "ninja", "jq", "ccache"):
        if not shutil.which(executable, path=environment.get("PATH")):
            raise RuntimeError(f"Missing build prerequisite: {executable}")
    original = subprocess.check_output(["git", "show", "HEAD:configs/defconfig.common"], cwd=BUILDER)
    (BUILDER / "configs/defconfig.common").write_bytes(original + DATE_CONFIG.encode())
    for target in LOCK["target_config_changes"]:
        name = f"configs/defconfig.{target}"
        original_target = subprocess.check_output(["git", "show", f"HEAD:{name}"], cwd=BUILDER)
        (BUILDER / name).write_bytes(original_target + target_config_addition(target))
    original_cmake = subprocess.check_output(["git", "show", "HEAD:CMakeLists.txt"], cwd=BUILDER)
    (BUILDER / "CMakeLists.txt").write_bytes(builder_cmake(original_cmake))
    shutil.copyfile(BASELINE / args.target / "dependencies.lock", BUILDER / "dependencies.lock")
    environment["ESP32BASE_TLS_TARGET"] = args.target
    # -s is essential: upstream's default installer pulls moving branches.
    # The official builder clears its own build/out directories between builds.
    run(["bash", "-c", 'source "$IDF_PATH/export.sh" && ./build.sh -s -t "$ESP32BASE_TLS_TARGET"'],
        BUILDER, environment)
    audit(args.target)
    audit_binary(args.target)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"TLS toolchain build stopped: {error}", file=sys.stderr)
        sys.exit(1)
