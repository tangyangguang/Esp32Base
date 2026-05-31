#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path):
    if path == "src/web/internal Web modules":
        return read_web_source()
    return (ROOT / path).read_text(encoding="utf-8")

WEB_SOURCE_PATHS = [
    "src/web/Esp32BaseWeb.cpp",
    "src/web/internal/WebInternal.h",
    "src/web/internal/WebContext.h",
    "src/web/internal/WebContext.cpp",
    "src/web/internal/WebAssets.cpp",
    "src/web/internal/WebAuth.cpp",
    "src/web/internal/WebFs.cpp",
    "src/web/internal/WebLayout.cpp",
    "src/web/internal/WebLogs.cpp",
    "src/web/internal/WebOta.cpp",
    "src/web/internal/WebResponse.cpp",
    "src/web/internal/WebRouting.cpp",
    "src/web/internal/WebStatus.cpp",
    "src/web/internal/WebTools.cpp",
    "src/web/internal/WebWifi.cpp",
    "src/web/internal/WebAppConfig.cpp",
]

def read_web_source() -> str:
    return "\n".join((ROOT / path).read_text(encoding="utf-8") for path in WEB_SOURCE_PATHS)


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
        "src/web/internal Web modules",
        'Esp32BaseConfig::getInt("eb_ui", "footer_mode"',
        "footer mode is not loaded from eb_ui.footer_mode",
    ),
    (
        "src/web/internal Web modules",
        'Esp32BaseConfig::setInt("eb_ui", "footer_mode"',
        "footer mode is not saved to eb_ui.footer_mode",
    ),
    (
        "src/web/internal Web modules",
        'if (raw == "status")',
        "System page parser does not accept status-only mode",
    ),
    (
        "src/web/internal Web modules",
        'sendFooterBarModeOption("status", "Status only", Esp32BaseWeb::FOOTER_BAR_STATUS_ONLY);',
        "System page does not show status-only option",
    ),
    (
        "src/web/internal Web modules",
        'g_server.on("/esp32base/tools/footer-bar", HTTP_POST, handleToolsFooterBarPost);',
        "footer bar POST route is not registered",
    ),
    (
        "src/web/internal Web modules",
        "case Esp32BaseWeb::FOOTER_BAR_OFF:",
        "sendFooter() does not handle OFF mode",
    ),
    (
        "src/web/internal Web modules",
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
