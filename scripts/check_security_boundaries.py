#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


errors = []

web_routing = read("src/web/internal/WebRouting.cpp")
web_core = read("src/web/Esp32BaseWeb.cpp")
web_wifi = read("src/web/internal/WebWifi.cpp")
wifi = read("src/network/Esp32BaseWiFi.inc")
profile = read("src/Esp32BaseProfile.h")

secret_log_patterns = (
    "password=%s",
    "password=%",
    "auth_saved_plain",
)
for path in (
    "src/web/internal/WebRouting.cpp",
    "src/web/Esp32BaseWeb.cpp",
    "src/web/internal/WebWifi.cpp",
    "src/network/Esp32BaseWiFi.inc",
):
    text = read(path)
    for pattern in secret_log_patterns:
        if pattern in text:
            errors.append(f"{path}: must not log plaintext credentials via {pattern!r}")

if 'value=\'");\n    sendEscapedHtmlChunk(password)' in web_wifi or "sendEscapedHtmlChunk(password);" in web_wifi:
    errors.append("src/web/internal/WebWifi.cpp: WiFi password input must not echo the saved password")
for needle, message in (
    ("Leave empty to keep the saved password", "WiFi form must explain blank password keeps the saved secret"),
    ("name='clear_password'", "WiFi form must provide an explicit way to clear the stored password for open networks"),
    ("password_state=", "WiFi submit log must record only password state, not the password"),
):
    if needle not in web_wifi:
        errors.append(f"src/web/internal/WebWifi.cpp: {message}")

if "ESP32BASE_WEB_ALLOW_INSECURE_DEFAULT_AUTH" not in profile:
    errors.append("src/Esp32BaseProfile.h: missing explicit insecure default auth opt-in macro")
if 'applyPlainAuth(g_defaultAuthSet ? g_defaultAuthUser : "admin"' in web_routing:
    errors.append("src/web/internal/WebRouting.cpp: Web auth must not silently fall back to admin/admin")
if "auth_default_missing" not in web_core:
    errors.append("src/web/Esp32BaseWeb.cpp: Web begin must fail closed when no stored/default auth is configured")
if "source=insecure_builtin" not in web_core:
    errors.append("src/web/Esp32BaseWeb.cpp: insecure built-in default auth must be explicitly logged when enabled")

docs = {
    "README.md": "Web/Auth 不再内置启用 admin/admin",
    "docs/03_api.md": "未设置应用默认认证且没有已保存认证时，Web 服务不会启动",
    "docs/04_web.md": "WiFi 密码字段不会回显已保存密码",
    "docs/05_ota.md": "Web OTA 认证来自当前 Web Auth",
    "docs/10_known_limitations.md": "Web/Auth 不再内置启用 admin/admin",
    "CHANGELOG.md": "Web/WiFi/Auth 凭据泄露面收敛",
}
for path, needle in docs.items():
    if needle not in read(path):
        errors.append(f"{path}: missing security boundary marker {needle!r}")

if errors:
    for error in errors:
        print(error)
    raise SystemExit(1)

print("Security boundary checks passed")
