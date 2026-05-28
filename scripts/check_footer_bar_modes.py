#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path):
    return (ROOT / path).read_text(encoding="utf-8")


checks = [
    (
        "src/web/Esp32BaseWeb.h",
        "enum FooterBarMode : uint8_t",
        "missing FooterBarMode public enum",
    ),
    (
        "src/web/Esp32BaseWeb.h",
        "FOOTER_BAR_STATUS_ONLY",
        "missing status-only footer mode",
    ),
    (
        "src/web/Esp32BaseWeb.h",
        "static bool setFooterBarMode(FooterBarMode mode);",
        "missing setFooterBarMode() public API",
    ),
    (
        "src/web/Esp32BaseWeb.h",
        "static FooterBarMode footerBarMode();",
        "missing footerBarMode() public API",
    ),
    (
        "src/web/Esp32BaseWeb.inc",
        'Esp32BaseConfig::getInt("eb_ui", "footer_mode"',
        "footer mode is not loaded from eb_ui.footer_mode",
    ),
    (
        "src/web/Esp32BaseWeb.inc",
        'Esp32BaseConfig::setInt("eb_ui", "footer_mode"',
        "footer mode is not saved to eb_ui.footer_mode",
    ),
    (
        "src/web/Esp32BaseWeb.inc",
        'if (raw == "status")',
        "System page parser does not accept status-only mode",
    ),
    (
        "src/web/Esp32BaseWeb.inc",
        'sendFooterBarModeOption("status", "Status only", Esp32BaseWeb::FOOTER_BAR_STATUS_ONLY);',
        "System page does not show status-only option",
    ),
    (
        "src/web/Esp32BaseWeb.inc",
        'g_server.on("/esp32base/tools/footer-bar", HTTP_POST, handleToolsFooterBarPost);',
        "footer bar POST route is not registered",
    ),
    (
        "src/web/Esp32BaseWeb.inc",
        "case Esp32BaseWeb::FOOTER_BAR_OFF:",
        "sendFooter() does not handle OFF mode",
    ),
    (
        "src/web/Esp32BaseWeb.inc",
        "case Esp32BaseWeb::FOOTER_BAR_STATUS_ONLY:",
        "sendFooter() does not handle status-only mode",
    ),
]


docs = {
    "README.md": "底部横条可在 System 页面配置为 Off、Status only 或 Links + status",
    "docs/03_api.md": "FooterBarMode",
    "docs/04_web.md": "Footer bar 模式设置只接受 Off、Status only、Links + status",
    "docs/09_release_checklist.md": "Footer bar 可切换 Off、Status only、Links + status",
    "CHANGELOG.md": "Footer bar 运行时显示模式",
}


errors = []
for path, needle, message in checks:
    if needle not in read(path):
        errors.append(f"{path}: {message}")

for path, needle in docs.items():
    if needle not in read(path):
        errors.append(f"{path}: missing documentation marker {needle!r}")

if errors:
    for error in errors:
        print(error)
    raise SystemExit(1)

print("Footer bar mode checks passed")
