#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path):
    return (ROOT / path).read_text(encoding="utf-8")


header = read("src/web/Esp32BaseWeb.h")
errors = []

routes_block = header.split("#ifndef ESP32BASE_WEB_MAX_NAV_ITEMS", 1)[0]
for marker in (
    "#define ESP32BASE_WEB_MAX_ROUTES 24",
):
    if marker not in routes_block:
        errors.append(f"src/web/Esp32BaseWeb.h: missing route default marker {marker!r}")

docs = {
    "README.md": "Web 应用路由默认容量统一为 24",
    "docs/04_web.md": "#define ESP32BASE_WEB_MAX_ROUTES 24",
    "docs/06_memory_budget.md": "#define ESP32BASE_WEB_MAX_ROUTES 24",
    "docs/08_arduino_core_compat.md": "Web route 默认仍统一为 24",
    "docs/10_known_limitations.md": "Web route 默认仍统一为 24",
}

for path, needle in docs.items():
    if needle not in read(path):
        errors.append(f"{path}: missing route default marker {needle!r}")

if errors:
    for error in errors:
        print(error)
    raise SystemExit(1)

print("Web route default checks passed")
