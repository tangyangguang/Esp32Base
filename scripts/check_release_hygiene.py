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
memory_budget = read("docs/06_memory_budget.md")

for path, text in (
    (".gitignore", read(".gitignore")),
    ("library.json", library),
    (".libraryignore", libraryignore),
    (".piopmignore", piopmignore),
):
    if "docs/superpowers" not in text:
        errors.append(f"{path}: release/export filters must exclude docs/superpowers")
    if "__pycache__" not in text or "*.py[cod]" not in text:
        errors.append(f"{path}: release/export filters must exclude Python cache files")
    if "idf_component.yml" not in text:
        errors.append(f"{path}: release/export filters must exclude generated idf_component.yml files")

for path, text in (
    ("library.json", library),
    (".libraryignore", libraryignore),
    (".piopmignore", piopmignore),
):
    if "docs/13_rtc_time_source_implementation_plan.md" not in text:
        errors.append(f"{path}: release/export filters must exclude implementation-plan process docs")

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
if "| FULL | 1031040 |" not in memory_budget:
    errors.append("docs/06_memory_budget.md: ESP32/Core2 FULL size table must be refreshed to current build")

docs = {
    "README.md": "发布包排除 docs/superpowers",
    "docs/01_architecture.md": "OTA boot 初始化",
    "docs/09_release_checklist.md": "docs/superpowers",
}
for path, needle in docs.items():
    if needle not in read(path):
        errors.append(f"{path}: missing release hygiene marker {needle!r}")

if errors:
    for error in errors:
        print(error)
    raise SystemExit(1)

print("Release hygiene checks passed")
