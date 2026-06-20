#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


assets = read("src/web/internal/WebAssets.cpp")
web = read("src/web/Esp32BaseWeb.cpp")
ota = read("src/web/internal/WebOta.cpp")
docs = read("docs/11_web_ui_baseline.md")
gallery = read("examples/web_ui_gallery/src/main.cpp")
gallery_selftest = gallery.split("#undef RUN_SELFTEST", 1)[0]
full_demo = read("examples/full_demo/src/main.cpp")

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

if gallery.count('RUN_SELFTEST("GET", "/esp32base/ui.css"') > 7:
    errors.append("web_ui_gallery selftest must not lock detailed CSS implementation strings.")
if "<header class='pagehead'><h1>Full Demo</h1></header><div class='statusgrid'>" in full_demo:
    errors.append("full_demo selftest must not lock adjacent pagehead/statusgrid HTML ordering.")
if "<section class='panel statuspage'><h2>OTA diagnostics</h2><div class='tablewrap'><table class='kv'>" in full_demo:
    errors.append("full_demo selftest must not lock OTA diagnostics wrapper ordering.")
for locked in (
    "<div class='tablewrap'><table class='kv'>",
    "<th>System logs</th><td><span class='tag ok'>enabled</span>",
    "<section class='panel dangerpanel'><h2>Clear WiFi</h2>",
    "<section class='panel logpanel'><div class='tablewrap'><table class='logmeta'>",
    "<th>Max per file</th><td>32.00 KB",
    "class='active' href='/esp32base/logs?segment=0'><span class='segname'>current-0",
    "<section class='panel dangerpanel'><h2>Restart device</h2>",
    "id='acbox' class='confirmbox' aria-hidden='true'><h2>Confirm changes</h2>",
    "<section class='panel formpanel wifipanel'><h2>Credentials</h2><form class='editform'",
    "<section class='panel formpanel authpanel'><h2>Credentials</h2><form class='editform'",
    "<section class='panel formpanel uploadpanel'><h2>Firmware upload</h2>",
    "<section class='panel actionpanel'><h2>System settings</h2>",
    "<section class='panel formpanel hostpanel'><h2>Hostname</h2>",
    "<footer class='footerbar'><span class='syslinks'>",
):
    if locked in full_demo:
        errors.append("full_demo selftest must not lock adjacent helper HTML ordering.")
for locked in (
    "<dialog id='native-confirm' class='panel eb-modal'>",
    "<input type='submit' class='btnlink info' value='开始'>",
    "class='uactions readonly'><span class='uvalue'>正常</span>",
    "class='uactions'><span class='uvalue'>空闲</span><a class='btnlink info' href='/esp32base/tools'>查看</a>",
    "<footer class='footerbar'><span class='syslinks'><a href='/esp32base'>Status</a><a href='/esp32base/logs'>System Logs</a><a href='/esp32base/app-config'>App Config</a>",
):
    if locked in gallery_selftest:
        errors.append("web_ui_gallery selftest must not lock adjacent helper HTML ordering.")

if errors:
    for error in errors:
        print(error)
    raise SystemExit(1)

print("Web UI baseline checks passed")
