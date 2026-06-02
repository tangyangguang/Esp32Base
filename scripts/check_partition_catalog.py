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
        "app0": (0x10000, 0x140000),
        "app1": (0x150000, 0x140000),
        "spiffs": (0x290000, 0x160000),
        "coredump": (0x3F0000, 0x10000),
    },
    "partitions/esp32-4mb-ota-large-app.csv": {
        "nvs": (0x9000, 0x5000),
        "otadata": (0xE000, 0x2000),
        "app0": (0x10000, 0x160000),
        "app1": (0x170000, 0x160000),
        "spiffs": (0x2D0000, 0x120000),
        "coredump": (0x3F0000, 0x10000),
    },
    "partitions/esp32-4mb-ota-large-fs.csv": {
        "nvs": (0x9000, 0x5000),
        "otadata": (0xE000, 0x2000),
        "app0": (0x10000, 0x100000),
        "app1": (0x110000, 0x100000),
        "spiffs": (0x210000, 0x1E0000),
        "coredump": (0x3F0000, 0x10000),
    },
    "partitions/esp32-c3-4mb-ota-balanced.csv": {
        "nvs": (0x9000, 0x5000),
        "otadata": (0xE000, 0x2000),
        "app0": (0x10000, 0x140000),
        "app1": (0x150000, 0x140000),
        "spiffs": (0x290000, 0x160000),
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

docs = [
    ("README.md", "应用项目强烈推荐直接选择 `partitions/` 中已有分区表"),
    ("README.md", "单 app slot（最大固件）"),
    ("README.md", "LittleFS（最大文件数据）"),
    ("README.md", "`partitions/esp32-4mb-ota-large-fs.csv`"),
    ("docs/02_profiles.md", "应用项目强烈推荐直接选择 `partitions/` 中已有分区表"),
    ("docs/06_memory_budget.md", "除非硬件容量、OTA 策略或持久化容量确实不匹配，否则不推荐业务项目自定义分区表"),
    ("docs/06_memory_budget.md", "LittleFS（最大文件数据）"),
    ("docs/06_memory_budget.md", "1.38 MB / 0x160000"),
    ("docs/09_release_checklist.md", "`partitions/esp32-4mb-ota-large-app.csv`"),
    ("docs/09_release_checklist.md", "`partitions/esp32-4mb-ota-large-fs.csv`"),
]
for path, needle in docs:
    if needle not in read(path):
        errors.append(f"{path}: missing partition catalog marker {needle!r}")

if errors:
    for error in errors:
        print(error)
    raise SystemExit(1)

print("Partition catalog checks passed")
