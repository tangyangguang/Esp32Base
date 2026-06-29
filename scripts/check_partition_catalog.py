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


expected = {
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

errors = []
for path, partitions in expected.items():
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

print("Partition catalog checks passed")
