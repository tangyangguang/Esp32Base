#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[1]
ntp_source = (root / "src/network/Esp32BaseNtp.inc").read_text()
time_source = (root / "src/runtime/Esp32BaseTime.inc").read_text()

for forbidden in (
    'setInt("eb_sys", "time_boot_id"',
    'getInt("eb_sys", "time_boot_id"',
):
    if forbidden in ntp_source or forbidden in time_source:
        raise SystemExit(f"NTP boot session must not persist {forbidden} during startup")

required = (
    "Esp32BaseSystem::bootCount()",
    "source=boot_count",
)
for marker in required:
    if marker not in time_source:
        raise SystemExit(f"missing Time boot session marker: {marker}")

if "Esp32BaseSystem::bootCount()" in ntp_source:
    raise SystemExit("NTP must delegate boot session ownership to Esp32BaseTime")
if "Esp32BaseTime::resolveCurrentBootEvent(bootId, uptimeSec, epochSec)" not in ntp_source:
    raise SystemExit("NTP must delegate current-boot event resolution to Esp32BaseTime")

print("NTP boot session checks passed")
