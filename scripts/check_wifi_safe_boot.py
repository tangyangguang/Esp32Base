#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path):
    return (ROOT / path).read_text(encoding="utf-8")


errors = []
wifi = read("src/network/Esp32BaseWiFi.inc")

source_checks = [
    ("ESP32BASE_WIFI_SAFE_BOOT_MAX_RESETS", "missing configurable guarded reset threshold"),
    ('"sta_guard"', "missing STA boot guard NVS marker"),
    ('"sta_rst"', "missing guarded reset counter"),
    ('"sta_pause"', "missing paused credential marker"),
    ("esp_reset_reason()", "missing reset reason inspection before STA retry"),
    ("ESP_RST_BROWNOUT", "missing brownout reset classification"),
    ("ESP_RST_PANIC", "missing panic reset classification"),
    ("ESP_RST_TASK_WDT", "missing watchdog reset classification"),
    ("sta_safe_boot_pause", "missing explicit safe boot pause log"),
    ("sta_safe_boot_resume", "missing explicit safe boot resume log"),
    ("sta_safe_boot_cleared", "missing explicit guard clear log"),
    ("startConfigPortal();", "safe boot recovery must fall back to config portal"),
]

for needle, message in source_checks:
    if needle not in wifi:
        errors.append(f"src/network/Esp32BaseWiFi.inc: {message}")

docs = {
    "docs/03_api.md": "STA 安全启动保护",
    "docs/04_web.md": "STA 安全启动保护",
    "docs/07_diagnostics.md": "sta_safe_boot_pause",
    "docs/10_known_limitations.md": "STA 安全启动保护",
}

for path, needle in docs.items():
    if needle not in read(path):
        errors.append(f"{path}: missing documentation marker {needle!r}")

if errors:
    for error in errors:
        print(error)
    raise SystemExit(1)

print("WiFi safe boot checks passed")
