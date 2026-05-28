#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[1]
source = (root / "src/network/Esp32BaseNtp.inc").read_text()

for forbidden in (
    'setInt("eb_sys", "time_boot_id"',
    'getInt("eb_sys", "time_boot_id"',
):
    if forbidden in source:
        raise SystemExit(f"NTP boot session must not persist {forbidden} during startup")

required = (
    "Esp32BaseSystem::bootCount()",
    "source=boot_count",
)
for marker in required:
    if marker not in source:
        raise SystemExit(f"missing NTP boot session marker: {marker}")

print("NTP boot session checks passed")
