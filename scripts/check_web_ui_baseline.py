#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


assets = read("src/web/internal/WebAssets.cpp")
web = read("src/web/Esp32BaseWeb.cpp")
ota = read("src/web/internal/WebOta.cpp")
docs = read("docs/11_web_ui_baseline.md")

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
]

errors = []
for text, needle, message in checks:
    if needle not in text:
        errors.append(message)

enhancement_checks = [
    (
        assets,
        "function ebAjaxSubmit",
        "WEB_HEAD must include the AJAX submit runtime.",
    ),
    (
        assets,
        ".eb-dialog{",
        "CSS must include dialog shell styles.",
    ),
    (
        assets,
        "dialog::backdrop",
        "CSS must style native dialog backdrops.",
    ),
    (
        assets,
        "dialog.panel",
        "CSS must normalize native dialog.panel layout.",
    ),
    (
        assets,
        "dialog .fieldgrid{margin:0}",
        "Native dialog forms must align field grids naturally.",
    ),
    (
        assets,
        ".eb-inline-edit{",
        "CSS must include inline edit styles.",
    ),
    (
        web,
        "sendInfoRowInlineEdit",
        "Esp32BaseWeb must expose inline edit helper implementation.",
    ),
    (
        web,
        "sendInfoRowDialogForm",
        "Esp32BaseWeb must expose dialog form helper implementation.",
    ),
    (
        web,
        "sendAjaxReplace",
        "Esp32BaseWeb must expose AJAX replace JSON helper.",
    ),
]
for text, needle, message in enhancement_checks:
    if needle not in text:
        errors.append(message)

general_uactions = ".uactions .btnlink,.uactions input[type=submit]{min-width:96px"
toollinks_override = ".toollinks .uactions .btnlink{min-width:72px;width:72px"
general_pos = assets.find(general_uactions)
override_pos = assets.find(toollinks_override)
if general_pos < 0:
    errors.append("General compact action buttons must keep a stable minimum width.")
if override_pos < 0:
    errors.append("Tool link action buttons must reset min-width to fit their compact action column.")

if errors:
    for error in errors:
        print(error)
    raise SystemExit(1)

print("Web UI baseline checks passed")
