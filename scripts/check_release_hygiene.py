#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def parse_partition_csv(path: str):
    entries = {}
    for raw in read(path).splitlines():
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        parts = [part.strip() for part in line.split(",")]
        if len(parts) < 5:
            continue
        name, typ, subtype, offset, size = parts[:5]
        entries[name] = {
            "type": typ,
            "subtype": subtype,
            "offset": int(offset, 0),
            "size": int(size, 0),
        }
    return entries


errors = []

license_path = ROOT / "LICENSE"
if not license_path.exists() or "MIT License" not in license_path.read_text(encoding="utf-8"):
    errors.append("LICENSE: declared MIT license text must be present in the release")

library = read("library.json")
libraryignore = read(".libraryignore")
piopmignore = read(".piopmignore")
for path, text in (
    (".gitignore", read(".gitignore")),
    ("library.json", library),
    (".libraryignore", libraryignore),
    (".piopmignore", piopmignore),
):
    if ".piohome" not in text:
        errors.append(f"{path}: release/export filters must exclude the isolated PlatformIO core directory")
    if "__pycache__" not in text or "*.py[cod]" not in text:
        errors.append(f"{path}: release/export filters must exclude Python cache files")
    if "idf_component.yml" not in text:
        errors.append(f"{path}: release/export filters must exclude generated idf_component.yml files")
    if path != ".gitignore" and "local_secrets.h" not in text:
        errors.append(f"{path}: release/export filters must exclude MQTT local secrets")

mqtt_ini = read("examples/mqtt_tls/platformio.ini")
for dependency in (
    "Preferences",
    "WiFi",
    "DNSServer",
    "ESPmDNS",
    "LittleFS",
    "WebServer",
    "Update",
    "ESP32 Async UDP",
    "FS",
    "Networking",
    "Hash",
):
    if f"\n  {dependency}\n" not in mqtt_ini:
        errors.append(
            f"examples/mqtt_tls/platformio.ini: external IOT build must declare built-in {dependency}"
        )
if "lib_ldf_mode = deep+" in mqtt_ini:
    errors.append("examples/mqtt_tls/platformio.ini: external build must work with default chain LDF")

for framework_library in (
    "AsyncUDP",
    "DNSServer",
    "ESPmDNS",
    "FS",
    "Hash",
    "LittleFS",
    "Network",
    "Preferences",
    "Update",
    "WebServer",
    "WiFi",
    "Wire",
):
    marker = f"framework-arduinoespressif32/libraries/{framework_library}/src"
    if marker not in library:
        errors.append(f"library.json: missing private implementation include path for {framework_library}")

for path in ("src/Esp32BaseProfile.h", "src/runtime/Esp32BaseFs.inc"):
    text = read(path)
    for marker in ("AUTO_FORMAT", "formatOnFail", "LittleFS.begin(true", "auto-format requested"):
        if marker in text:
            errors.append(f"{path}: forbidden automatic format marker {marker!r}")

all_docs = read("README.md") + "\n" + "\n".join(
    path.read_text(encoding="utf-8") for path in sorted((ROOT / "docs").glob("*.md"))
)
for marker in (
    "ESP32BASE_OTA_DISABLE_BROWNOUT_DURING_WRITE",
    "ESP32BASE_RESTART_LOG_CAPACITY",
):
    if marker in all_docs:
        errors.append(f"documentation: unsupported configuration marker {marker!r}")

for ini_path in sorted((ROOT / "examples").glob("*/platformio.ini")):
    text = ini_path.read_text(encoding="utf-8")
    if "lib_extra_dirs = ../.." in text and "lib_ignore = .piohome" not in text:
        errors.append(
            f"{ini_path.relative_to(ROOT)}: examples scanning the repository root must ignore .piohome"
        )
    if "arduino3]" in text and "pioarduino_core3_preflight.py" not in text:
        errors.append(
            f"{ini_path.relative_to(ROOT)}: Core 3.x env must run the isolated package preflight"
        )

expected_partitions = {
    "partitions/esp32-4mb-ota-balanced.csv": {
        "nvs": (0x9000, 0x5000),
        "otadata": (0xE000, 0x2000),
        "app0": (0x10000, 0x180000),
        "app1": (0x190000, 0x180000),
        "spiffs": (0x310000, 0xE0000),
        "coredump": (0x3F0000, 0x10000),
    },
    "partitions/esp32-c3-4mb-ota-balanced.csv": {
        "nvs": (0x9000, 0x5000),
        "otadata": (0xE000, 0x2000),
        "app0": (0x10000, 0x180000),
        "app1": (0x190000, 0x180000),
        "spiffs": (0x310000, 0xE0000),
        "coredump": (0x3F0000, 0x10000),
    },
    "partitions/esp32-s3-8mb-ota-balanced.csv": {
        "nvs": (0x9000, 0x5000),
        "otadata": (0xE000, 0x2000),
        "app0": (0x10000, 0x240000),
        "app1": (0x250000, 0x240000),
        "spiffs": (0x490000, 0x360000),
        "coredump": (0x7F0000, 0x10000),
    },
}
for path, partitions in expected_partitions.items():
    csv_path = ROOT / path
    if not csv_path.exists():
        errors.append(f"{path}: missing recommended partition table")
        continue
    entries = parse_partition_csv(path)
    for name, (offset, size) in partitions.items():
        entry = entries.get(name)
        if entry is None:
            errors.append(f"{path}: missing partition {name}")
            continue
        if entry["offset"] != offset or entry["size"] != size:
            errors.append(
                f"{path}: {name} expected offset=0x{offset:X} size=0x{size:X}, "
                f"got offset=0x{entry['offset']:X} size=0x{entry['size']:X}"
            )

if errors:
    for error in errors:
        print(error)
    raise SystemExit(1)

print("Release hygiene checks passed")
