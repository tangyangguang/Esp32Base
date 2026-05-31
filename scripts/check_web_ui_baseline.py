#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


assets = read("src/web/internal/WebAssets.cpp")
web = read("src/web/Esp32BaseWeb.cpp")
ota = read("src/web/internal/WebOta.cpp")
docs = read("docs/11_web_ui_baseline.md")
changelog = read("CHANGELOG.md")

checks = [
    (
        assets,
        "--eb-primary-hover",
        "CSS must expose a primary hover variable for skinning.",
    ),
    (
        assets,
        "--eb-button-soft",
        "CSS must expose soft button variables for low-emphasis actions.",
    ),
    (
        assets,
        "min-height:30px",
        "Default buttons should be visually compact at 30px high.",
    ),
    (
        assets,
        ".btnlink,input.btnlink",
        "Button links and input buttons must share the same button-link style.",
    ),
    (
        web,
        "class='btnlink",
        "sendInfoRowCompactForm() must emit btnlink tone classes for form actions.",
    ),
    (
        ota,
        "<div class='tablewrap'><table class='kv'>",
        "OTA diagnostics must use a valid kv table wrapper.",
    ),
    (
        docs,
        "按钮视觉层级",
        "UI baseline docs must describe the refined button hierarchy.",
    ),
    (
        changelog,
        "Web UI 按钮视觉层级",
        "CHANGELOG must describe the UI baseline button refinement.",
    ),
]

errors = []
for text, needle, message in checks:
    if needle not in text:
        errors.append(message)

if errors:
    for error in errors:
        print(error)
    raise SystemExit(1)

print("Web UI baseline checks passed")
