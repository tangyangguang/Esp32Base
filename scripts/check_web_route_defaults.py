#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path):
    return (ROOT / path).read_text(encoding="utf-8")


header = read("src/web/Esp32BaseWeb.h")
errors = []

if "#define ESP32BASE_WEB_MAX_ROUTES 24" not in header:
    errors.append("src/web/Esp32BaseWeb.h: ESP32BASE_WEB_MAX_ROUTES default is not 24")

routes_block = header.split("#ifndef ESP32BASE_WEB_MAX_NAV_ITEMS", 1)[0]
if "CONFIG_IDF_TARGET_ESP32C3" in routes_block:
    errors.append("src/web/Esp32BaseWeb.h: route default still has ESP32-C3 special case")

docs = {
    "README.md": "ESP32BASE_WEB_MAX_ROUTES=24",
    "docs/04_web.md": "#define ESP32BASE_WEB_MAX_ROUTES 24",
    "docs/06_memory_budget.md": "#define ESP32BASE_WEB_MAX_ROUTES 24",
    "CHANGELOG.md": "Web 应用路由默认容量统一调整为 24",
}

for path, needle in docs.items():
    if needle not in read(path):
        errors.append(f"{path}: missing route default marker {needle!r}")

if errors:
    for error in errors:
        print(error)
    raise SystemExit(1)

print("Web route default checks passed")
