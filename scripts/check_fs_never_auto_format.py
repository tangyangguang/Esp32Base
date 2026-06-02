#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path):
    return (ROOT / path).read_text(encoding="utf-8")


errors = []

for path in ("src/Esp32BaseProfile.h", "src/runtime/Esp32BaseFs.inc"):
    text = read(path)
    forbidden = (
        "AUTO_FORMAT",
        "formatOnFail",
        "LittleFS.begin(true",
        "auto-format requested",
    )
    for needle in forbidden:
        if needle in text:
            errors.append(f"{path}: forbidden automatic format marker {needle!r}")

docs = {
    "README.md": "LittleFS mount failed 时不会自动格式化",
    "docs/03_api.md": "LittleFS mount failed 时不会自动格式化",
    "docs/09_release_checklist.md": "任何启动路径都不得自动格式化 LittleFS",
    "docs/10_known_limitations.md": "LittleFS 默认不自动格式化",
}

for path, needle in docs.items():
    if needle not in read(path):
        errors.append(f"{path}: missing no-auto-format documentation marker {needle!r}")

if errors:
    for error in errors:
        print(error)
    raise SystemExit(1)

print("FS never-auto-format checks passed")
