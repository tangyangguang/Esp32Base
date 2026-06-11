#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


errors = []
wifi = read("src/network/Esp32BaseWiFi.inc")
web_response = read("src/web/internal/WebResponse.cpp")

wifi_checks = [
    (
        "const bool slowBackoff = g_wifiRetryCount >= ESP32BASE_WIFI_RETRY_FAST_COUNT;",
        "WiFi retry logging must detect slow recurring backoff",
    ),
    (
        'ESP32BASE_LOG_I("wifi", "station_connect_timeout ssid=%s status=%s status_code=%u elapsed=%lu ms rssi=%ld retry_phase=slow"',
        "slow recurring WiFi connect timeout must be INFO to avoid default WARN FileLog writes every minute",
    ),
    (
        'ESP32BASE_LOG_W("wifi", "station_connect_timeout ssid=%s status=%s status_code=%u elapsed=%lu ms rssi=%ld retry_phase=fast"',
        "initial WiFi connect timeouts should remain WARN",
    ),
    (
        'ESP32BASE_LOG_I("wifi", "%s attempt=%u retry_in=%lu ms mode=%s status=%s status_code=%u rssi=%ld"',
        "slow recurring WiFi retry scheduling must be INFO",
    ),
    (
        'ESP32BASE_LOG_W("wifi", "%s attempt=%u retry_in=%lu ms mode=%s status=%s status_code=%u rssi=%ld"',
        "initial WiFi retry scheduling should remain WARN",
    ),
]

for needle, message in wifi_checks:
    if needle not in wifi:
        errors.append(f"src/network/Esp32BaseWiFi.inc: {message}")

web_checks = [
    (
        'ESP32BASE_LOG_I("web", "response_client_disconnected uri=%s"',
        "client disconnects during HTTP responses should be INFO because they can be caused by normal browser/network behavior",
    ),
]

for needle, message in web_checks:
    if needle not in web_response:
        errors.append(f"src/web/internal/WebResponse.cpp: {message}")

docs = {
    "README.md": "长期离线或路由器不可用时，WiFi 慢速 backoff 的重复重试日志降为 INFO",
    "docs/03_api.md": "进入慢速 backoff 后，重复的连接超时和重试排程日志降为 INFO",
    "docs/07_diagnostics.md": "慢速 backoff 阶段的重复 WiFi 重试日志使用 INFO",
}

for path, needle in docs.items():
    if needle not in read(path):
        errors.append(f"{path}: missing flash write log policy marker {needle!r}")

if errors:
    for error in errors:
        print(error)
    raise SystemExit(1)

print("Flash write log policy checks passed")
