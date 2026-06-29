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

library = read("library.json")
libraryignore = read(".libraryignore")
piopmignore = read(".piopmignore")
for path, text in (
    (".gitignore", read(".gitignore")),
    ("library.json", library),
    (".libraryignore", libraryignore),
    (".piopmignore", piopmignore),
):
    if "__pycache__" not in text or "*.py[cod]" not in text:
        errors.append(f"{path}: release/export filters must exclude Python cache files")
    if "idf_component.yml" not in text:
        errors.append(f"{path}: release/export filters must exclude generated idf_component.yml files")

stale_process_refs = (
    ("docs/", "super" + "powers"),
    (".", "super" + "powers"),
    ("_", "implementation" + "_plan.md"),
    ("Super", "powers"),
)
for path in (
    "README.md",
    "docs/09_release_checklist.md",
    "docs/11_web_ui_baseline.md",
    "library.json",
    ".gitignore",
    ".libraryignore",
    ".piopmignore",
):
    text = read(path)
    for parts in stale_process_refs:
        marker = "".join(parts)
        if marker in text:
            errors.append(f"{path}: stale process reference remains")

for path in ("src/Esp32BaseProfile.h", "src/runtime/Esp32BaseFs.inc"):
    text = read(path)
    for marker in ("AUTO_FORMAT", "formatOnFail", "LittleFS.begin(true", "auto-format requested"):
        if marker in text:
            errors.append(f"{path}: forbidden automatic format marker {marker!r}")

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
