#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def function_body(text: str, signature: str) -> str:
    start = text.find(signature)
    if start < 0:
        raise ValueError(f"missing function {signature}")
    brace = text.find("{", start)
    if brace < 0:
        raise ValueError(f"missing function body for {signature}")
    depth = 0
    for pos in range(brace, len(text)):
        if text[pos] == "{":
            depth += 1
        elif text[pos] == "}":
            depth -= 1
            if depth == 0:
                return text[brace + 1:pos]
    raise ValueError(f"unterminated function body for {signature}")


def require(condition: bool, message: str) -> None:
    if not condition:
        errors.append(message)


errors: list[str] = []

app_config = read("src/web/internal/WebAppConfig.cpp")
header = read("src/web/Esp32BaseAppConfig.h")

try:
    validate_all = function_body(app_config, "bool validateAllSubmitted(")
    write_field = function_body(app_config, "bool writeSubmittedField(")
    handle_submit = function_body(app_config, "void handleAppConfigSubmit(")
    submitted_string = function_body(app_config, "bool Esp32BaseAppConfig::submittedString(")
    submitted_int = function_body(app_config, "bool Esp32BaseAppConfig::submittedInt(")
    submitted_decimal = function_body(app_config, "bool Esp32BaseAppConfig::submittedDecimal(")
    submitted_bool = function_body(app_config, "bool Esp32BaseAppConfig::submittedBool(")
    submitted_enum = function_body(app_config, "bool Esp32BaseAppConfig::submittedEnum(")
except ValueError as exc:
    errors.append(str(exc))
    validate_all = write_field = handle_submit = ""
    submitted_string = submitted_int = submitted_decimal = submitted_bool = submitted_enum = ""

rev_pos = handle_submit.find("g_appConfigRevision")
validate_pos = handle_submit.find("validateAllSubmitted")
validate_guard_pos = handle_submit.find("if (!validateAllSubmitted")
write_pos = handle_submit.find("writeSubmittedField")
save_pos = handle_submit.find("g_appConfigSaveCallback")
partial_pos = handle_submit.find("partial=1")

require(rev_pos >= 0, "handleAppConfigSubmit() must check the RAM revision before validation or writes")
require(validate_pos >= 0, "handleAppConfigSubmit() must validate the full submitted page")
require(validate_guard_pos >= 0, "handleAppConfigSubmit() must abort when full-page validation/veto fails")
require(write_pos >= 0, "handleAppConfigSubmit() must write submitted fields only after validation")
require(save_pos >= 0, "handleAppConfigSubmit() must call SaveCallback only after the write loop")
require(partial_pos >= 0, "handleAppConfigSubmit() must retain the existing partial-save response")
require(0 <= rev_pos < validate_pos < write_pos < save_pos, "App Config submit order must be revision -> validate -> write -> save callback")
require("sendAppConfigPage(error);\n        return;" in handle_submit[validate_guard_pos:write_pos],
        "Rejected full-page validation must return before the App Config write loop")

require("validateSubmittedField" in validate_all, "validateAllSubmitted() must keep existing field-level validation")
require("g_appConfigPageValidate" in validate_all, "validateAllSubmitted() must run the page-level save veto callback")
require(validate_all.find("validateSubmittedField") < validate_all.find("g_appConfigPageValidate"),
        "Page-level veto must run after each field has passed built-in and field validators")
require("g_appConfigSubmitContext = true;" in validate_all and "g_appConfigSubmitContext = false;" in validate_all,
        "Page-level veto must open and close the submitted-value read context")
require("Esp32BaseConfig::set" not in validate_all, "Validation/veto path must not write NVS")
require("writeSubmittedField" not in validate_all, "Validation/veto path must not trigger partial field saves")
require("return false;" in validate_all, "Rejected page validation must abort the submit before writes")

for needle in ("Esp32BaseConfig::setStr", "Esp32BaseConfig::setInt", "Esp32BaseConfig::setBool"):
    require(needle in write_field, f"writeSubmittedField() must remain the only App Config NVS write path for {needle}")

for name, body in (
    ("submittedString", submitted_string),
    ("submittedInt", submitted_int),
    ("submittedDecimal", submitted_decimal),
    ("submittedBool", submitted_bool),
    ("submittedEnum", submitted_enum),
):
    require("g_appConfigSubmitContext" in body, f"{name}() must only expose submitted values during page validation")
    require("findSubmittedRaw" in body, f"{name}() must read the current POST payload, not stored NVS")

for needle in (
    "using PageValidateCallback",
    "static bool setPageValidateCallback(PageValidateCallback callback);",
    "using SaveCallback",
    "static bool setSaveCallback(SaveCallback callback);",
):
    require(needle in header, f"Esp32BaseAppConfig public API must keep {needle}")

docs = {
    "README.md": "保存前 veto",
    "docs/03_api.md": "`SaveCallback` 是保存后通知，不用于拒绝保存",
    "docs/04_web.md": "只读状态、未来版本或业务版本不兼容",
    "docs/09_release_checklist.md": "保存前 veto",
    "CHANGELOG.md": "App Config 保存前 veto 回归",
}
for path, marker in docs.items():
    require(marker in read(path), f"{path}: missing App Config save-veto marker {marker!r}")

if errors:
    for error in errors:
        print(error)
    raise SystemExit(1)

print("App Config save-veto checks passed")
