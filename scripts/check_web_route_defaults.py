#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path):
    return (ROOT / path).read_text(encoding="utf-8")


header = read("src/web/Esp32BaseWeb.h")
errors = []

routes_block = header.split("#ifndef ESP32BASE_WEB_MAX_NAV_ITEMS", 1)[0]
for marker in (
    "#if defined(CONFIG_IDF_TARGET_ESP32C3)",
    "#define ESP32BASE_WEB_MAX_ROUTES 12",
    "#define ESP32BASE_WEB_MAX_ROUTES 16",
):
    if marker not in routes_block:
        errors.append(f"src/web/Esp32BaseWeb.h: missing conservative route default marker {marker!r}")

docs = {
    "README.md": "默认容量为 ESP32/ESP32-S3 16、ESP32-C3 12",
    "docs/04_web.md": "#define ESP32BASE_WEB_MAX_ROUTES 16",
    "docs/06_memory_budget.md": "#define ESP32BASE_WEB_MAX_ROUTES 16",
    "CHANGELOG.md": "Web 应用路由默认容量恢复为保守静态 RAM 配置",
}

for path, needle in docs.items():
    if needle not in read(path):
        errors.append(f"{path}: missing route default marker {needle!r}")

if errors:
    for error in errors:
        print(error)
    raise SystemExit(1)

print("Web route default checks passed")
