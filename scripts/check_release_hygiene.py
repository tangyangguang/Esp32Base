#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


errors = []

library = read("library.json")
libraryignore = read(".libraryignore")
piopmignore = read(".piopmignore")
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

if errors:
    for error in errors:
        print(error)
    raise SystemExit(1)

print("Release hygiene checks passed")
