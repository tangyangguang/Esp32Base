#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


errors = []

web_routing = read("src/web/internal/WebRouting.cpp")
web_core = read("src/web/Esp32BaseWeb.cpp")
web_auth = read("src/web/internal/WebAuth.cpp")
web_wifi = read("src/web/internal/WebWifi.cpp")
wifi = read("src/network/Esp32BaseWiFi.inc")
profile = read("src/Esp32BaseProfile.h")
base = read("src/Esp32Base.cpp")
webota = read("scripts/esp32base_webota.py")
web_docs = read("docs/04_web.md")
ota_docs = read("docs/05_ota.md")

plaintext_log_requirements = {
    "src/web/internal/WebRouting.cpp": (
        "auth_loaded user=%s password=%s source=stored",
        "auth_request context=%s user=%s password=%s result=failed",
    ),
    "src/web/Esp32BaseWeb.cpp": (
        "auth_loaded user=%s password=%s source=default",
        "default_auth_set user=%s password=%s applied=%s",
    ),
    "src/web/internal/WebWifi.cpp": (
        "wifi form submitted ssid=%s password=%s result=%s",
    ),
    "src/network/Esp32BaseWiFi.inc": (
        "station_connecting ssid=%s password=%s status=%s status_code=%u",
        "credentials_set ssid=%s password=%s persist=%s",
    ),
}
for path, needles in plaintext_log_requirements.items():
    text = read(path)
    for needle in needles:
        if needle not in text:
            errors.append(f"{path}: missing plaintext credential log marker {needle!r}")

if 'value=\'");\n    sendEscapedHtmlChunk(password)' in web_wifi or "sendEscapedHtmlChunk(password);" in web_wifi:
    errors.append("src/web/internal/WebWifi.cpp: WiFi password input must not echo the saved password")
for needle, message in (
    ("Leave empty to keep the saved password", "WiFi form must explain blank password keeps the saved secret"),
    ("name='clear_password'", "WiFi form must provide an explicit way to clear the stored password for open networks"),
    ("password=%s", "WiFi submit log must include the plaintext password for field debugging"),
):
    if needle not in web_wifi:
        errors.append(f"src/web/internal/WebWifi.cpp: {message}")

if "ESP32BASE_WEB_ALLOW_INSECURE_DEFAULT_AUTH" not in profile:
    errors.append("src/Esp32BaseProfile.h: missing explicit insecure default auth opt-in macro")
if 'applyPlainAuth(g_defaultAuthSet ? g_defaultAuthUser : "admin"' in web_routing:
    errors.append("src/web/internal/WebRouting.cpp: Web auth must not silently fall back to admin/admin")
if "auth_default_missing" not in web_core:
    errors.append("src/web/Esp32BaseWeb.cpp: Web begin must fail closed when no stored/default auth is configured")
if "g_startLocked" not in web_core or "bool Esp32BaseWeb::startLocked()" not in web_core:
    errors.append("src/web/Esp32BaseWeb.cpp: auth-missing fail-closed must latch startLocked after the first failure")
if "!Esp32BaseWeb::startLocked()" not in base:
    errors.append("src/Esp32Base.cpp: deferred Web start must stop retrying while Web start is locked")
if "source=insecure_builtin" not in web_core:
    errors.append("src/web/Esp32BaseWeb.cpp: insecure built-in default auth must be explicitly logged when enabled")
if '_option("esp32base_webota_user", "admin")' in webota or '_option("esp32base_webota_password", "admin")' in webota:
    errors.append("scripts/esp32base_webota.py: webota must not default Basic Auth to admin/admin")
if "Web OTA auth is required" not in webota:
    errors.append("scripts/esp32base_webota.py: webota must fail with a clear message when auth is not configured")
for forbidden in [
    'strlcpy(currentUser, g_server.arg("current_user").c_str()',
    'strlcpy(currentPass, g_server.arg("current_pass").c_str()',
    'strlcpy(newUser, g_server.arg("new_user").c_str()',
    'strlcpy(newPass, g_server.arg("new_pass").c_str()',
    'strlcpy(confirmPass, g_server.arg("confirm_pass").c_str()',
]:
    if forbidden in web_auth:
        errors.append("src/web/internal/WebAuth.cpp: auth submit must reject overlong form values instead of truncating g_server.arg()")
for needle in [
    'authReadArg("current_user", currentUser, sizeof(currentUser))',
    'authReadArg("current_pass", currentPass, sizeof(currentPass))',
    'authReadArg("new_user", newUser, sizeof(newUser))',
    'authReadArg("new_pass", newPass, sizeof(newPass))',
    'authReadArg("confirm_pass", confirmPass, sizeof(confirmPass))',
]:
    if needle not in web_auth:
        errors.append(f"src/web/internal/WebAuth.cpp: missing bounded auth form read {needle!r}")
if "- 回显当前密码" in web_docs:
    errors.append("docs/04_web.md: WiFi page docs must not claim the saved password is echoed")
if "custom_esp32base_webota_user = admin" in ota_docs or "custom_esp32base_webota_password = admin" in ota_docs:
    errors.append("docs/05_ota.md: Web OTA examples must not use admin/admin placeholders")

docs = {
    "README.md": "明文凭据日志是本库面向业务接入和现场调试的明确选择",
    "docs/03_api.md": "未设置应用默认认证且没有已保存认证时，Web 服务不会启动",
    "docs/04_web.md": "Web Auth 持久化使用 `eb_web.auth_user`、`eb_web.auth_pass`，并在 INFO 日志中输出明文用户名和密码",
    "docs/05_ota.md": "Web OTA 认证来自当前 Web Auth",
    "docs/10_known_limitations.md": "明文存储和明文日志是项目选择，不作为缺陷或待修风险评估",
}
for path, needle in docs.items():
    if needle not in read(path):
        errors.append(f"{path}: missing security boundary marker {needle!r}")

if errors:
    for error in errors:
        print(error)
    raise SystemExit(1)

print("Security boundary checks passed")
