#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


errors = []

library = read("library.json")
libraryignore = read(".libraryignore")
piopmignore = read(".piopmignore")
profiles = read("docs/02_profiles.md")
api_docs = read("docs/03_api.md")
arduino3_ensure = read("scripts/ensure_arduino3_platformio.py")
arduino3_preflight = read("scripts/pioarduino_core3_preflight.py")

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

if "#if ESP32BASE_PROFILE == ESP32BASE_PROFILE_NET" not in profiles:
    errors.append("docs/02_profiles.md: profile example must use numeric ESP32BASE_PROFILE comparison")
if "#if defined(ESP32BASE_PROFILE_NET)" in profiles:
    errors.append("docs/02_profiles.md: profile example must not use defined(ESP32BASE_PROFILE_NET)")
if "ESP32BASE_ENABLE_APP_CONFIG" not in profiles:
    errors.append("docs/02_profiles.md: bottom macro list must include ESP32BASE_ENABLE_APP_CONFIG")
if "`APP_CONFIG` 需要 `WEB`" not in profiles:
    errors.append("docs/02_profiles.md: dependency rules must document APP_CONFIG requires WEB")
if "保留用于兼容旧代码" in api_docs:
    errors.append("docs/03_api.md: must not explain current APIs as legacy compatibility")
if "esp32_full_arduino3" not in arduino3_ensure or "framework-arduinoespressif32" not in arduino3_ensure:
    errors.append("scripts/ensure_arduino3_platformio.py: must preinstall pioarduino Core 3.x packages")
if "pioarduino Core 3.x package preflight failed" not in arduino3_preflight:
    errors.append("scripts/pioarduino_core3_preflight.py: must guard pioarduino package availability")

if errors:
    for error in errors:
        print(error)
    raise SystemExit(1)

print("Release hygiene checks passed")
