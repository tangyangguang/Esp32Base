#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


errors = []

web_routing = read("src/web/internal/WebRouting.cpp")
web_core = read("src/web/Esp32BaseWeb.cpp")
web_header = read("src/web/Esp32BaseWeb.h")
web_auth = read("src/web/internal/WebAuth.cpp")
web_wifi = read("src/web/internal/WebWifi.cpp")
wifi = read("src/network/Esp32BaseWiFi.inc")
profile = read("src/Esp32BaseProfile.h")
capability_config = read("src/Esp32BaseCapabilityConfig.h")
base = read("src/Esp32Base.cpp")
webota = read("scripts/esp32base_webota.py")
web_docs = read("docs/04_web.md")
ota_docs = read("docs/05_ota.md")
mqtt = read("src/network/Esp32BaseMqtt.inc")
mqtt_header = read("src/network/Esp32BaseMqtt.h")

if 'value=\'");\n    sendEscapedHtmlChunk(password)' in web_wifi or "sendEscapedHtmlChunk(password);" in web_wifi:
    errors.append("src/web/internal/WebWifi.cpp: WiFi password input must not echo the saved password")
for path, text in (
    ("src/web/internal/WebRouting.cpp", web_routing),
    ("src/web/Esp32BaseWeb.cpp", web_core),
    ("src/web/internal/WebWifi.cpp", web_wifi),
    ("src/network/Esp32BaseWiFi.inc", wifi),
):
    if "password=%s" in text:
        errors.append(f"{path}: credential logs must not include plaintext password values")
if "name='clear_password'" not in web_wifi:
    errors.append("src/web/internal/WebWifi.cpp: WiFi form must provide an explicit way to clear the stored password for open networks")

if "ESP32BASE_WEB_ALLOW_INSECURE_DEFAULT_AUTH" not in capability_config:
    errors.append("src/Esp32BaseCapabilityConfig.h: missing explicit insecure default auth opt-in macro")
if 'applyPlainAuth(g_defaultAuthSet ? g_defaultAuthUser : "admin"' in web_routing:
    errors.append("src/web/internal/WebRouting.cpp: Web auth must not silently fall back to admin/admin")
if "bool Esp32BaseWeb::startLocked()" not in web_core or "static bool startLocked();" not in web_header:
    errors.append("src/web/Esp32BaseWeb.cpp: auth-missing fail-closed must expose a start-locked state")
if "!Esp32BaseWeb::startLocked()" not in base:
    errors.append("src/Esp32Base.cpp: deferred Web start must stop retrying while Web start is locked")
if '_option("esp32base_webota_user", "admin")' in webota or '_option("esp32base_webota_password", "admin")' in webota:
    errors.append("scripts/esp32base_webota.py: webota must not default Basic Auth to admin/admin")
for forbidden in [
    'strlcpy(currentUser, g_server.arg("current_user").c_str()',
    'strlcpy(currentPass, g_server.arg("current_pass").c_str()',
    'strlcpy(newUser, g_server.arg("new_user").c_str()',
    'strlcpy(newPass, g_server.arg("new_pass").c_str()',
    'strlcpy(confirmPass, g_server.arg("confirm_pass").c_str()',
]:
    if forbidden in web_auth:
        errors.append("src/web/internal/WebAuth.cpp: auth submit must reject overlong form values instead of truncating g_server.arg()")
if "- 回显当前密码" in web_docs:
    errors.append("docs/04_web.md: WiFi page docs must not claim the saved password is echoed")
if "custom_esp32base_webota_user = admin" in ota_docs or "custom_esp32base_webota_password = admin" in ota_docs:
    errors.append("docs/05_ota.md: Web OTA examples must not use admin/admin placeholders")

for forbidden in (
    "password=%s",
    "private_key=%s",
    "payload=%s",
    "skip_cert_common_name_check = true",
):
    if forbidden in mqtt:
        errors.append(f"src/network/Esp32BaseMqtt.inc: MQTT security boundary contains forbidden pattern {forbidden}")
if "EXPLICIT_PLAINTEXT" not in mqtt_header or "ESP32BASE_MQTT_ALLOW_PLAINTEXT" not in mqtt:
    errors.append("MQTT plaintext transport must require explicit API and build opt-in")
if "validCertificatePem(config.tls.caCertificatePem" not in mqtt:
    errors.append("MQTTS must fail closed when no application CA is configured")
if 'strcmp(pem, "NULL") != 0' not in mqtt:
    errors.append("MQTTS must reject ESP-MQTT's special CA verification disable value")

if errors:
    for error in errors:
        print(error)
    raise SystemExit(1)

print("Security boundary checks passed")
