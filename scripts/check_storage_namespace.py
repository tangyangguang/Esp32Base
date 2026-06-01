#!/usr/bin/env python3
"""Check Esp32Base-owned LittleFS namespace boundaries."""

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def function_body(text: str, signature: str) -> str:
    start = 0
    while True:
        start = text.find(signature, start)
        if start < 0:
            return ""
        brace = text.find("{", start)
        semi = text.find(";", start)
        if brace >= 0 and (semi < 0 or brace < semi):
            break
        start += len(signature)
    depth = 0
    for i in range(brace, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return text[brace:i + 1]
    return text[brace:]


def normalize_signature(signature: str) -> str:
    signature = re.sub(r"//.*", "", signature)
    signature = re.sub(r"/\*.*?\*/", "", signature, flags=re.S)
    signature = re.sub(r"\s+", " ", signature).strip()
    signature = signature.rstrip(";{").strip()
    signature = re.sub(r"\s*=\s*[^,\)]+", "", signature)
    signature = signature.replace(" &", "&").replace("& ", "& ")
    signature = signature.replace(" *", "*").replace("* ", "* ")
    signature = re.sub(r"\s*,\s*", ", ", signature)
    signature = re.sub(r"\s*\(\s*", "(", signature)
    signature = re.sub(r"\s*\)\s*", ")", signature)
    return signature


def extract_declaration(text: str, name: str) -> str:
    pattern = re.compile(rf"(?m)^[ \t]*(?!#)([A-Za-z_][\w:<>,\s*&]*\b{name}\s*\([^;{{}}]*\)\s*;)")
    match = pattern.search(text)
    return normalize_signature(match.group(1)) if match else ""


def extract_definition(text: str, name: str) -> str:
    pattern = re.compile(rf"(?m)^[ \t]*(?!#)([A-Za-z_][\w:<>,\s*&]*\b{name}\s*\([^;{{}}]*\))\s*\{{")
    match = pattern.search(text)
    return normalize_signature(match.group(1)) if match else ""


errors: list[str] = []

filelog_header = read("src/runtime/Esp32BaseFileLog.h")
filelog_source = read("src/runtime/Esp32BaseFileLog.inc")
app_events = read("src/runtime/Esp32BaseAppEventLog.inc")
web_fs = read("src/web/internal/WebFs.cpp")
web_internal = read("src/web/internal/WebInternal.h")
filelog_owns = function_body(web_fs, "bool fileLogOwnsPath(")

checks = [
    (
        "src/runtime/Esp32BaseFileLog.h",
        filelog_header,
        '#define ESP32BASE_EB_FILELOG_PATH "/esp32base/logs/system.log"',
        "FileLog default path must live under /esp32base/logs",
    ),
    (
        "src/runtime/Esp32BaseFileLog.inc",
        filelog_source,
        "ensureDirPath(g_fileLogPath)",
        "FileLog must create nested /esp32base/logs before opening segments",
    ),
    (
        "src/runtime/Esp32BaseAppEventLog.inc",
        app_events,
        'constexpr const char* kAppEventDir = "/esp32base/app-events";',
        "App Events directory must live under /esp32base",
    ),
    (
        "src/runtime/Esp32BaseAppEventLog.inc",
        app_events,
        'constexpr const char* kAppEventPath = "/esp32base/app-events/events.bin";',
        "App Events store file must live under /esp32base/app-events",
    ),
    (
        "src/runtime/Esp32BaseAppEventLog.inc",
        app_events,
        "ensureDirPath(kAppEventPath)",
        "App Events must create nested /esp32base/app-events before creating the store",
    ),
    (
        "src/web/internal/WebFs.cpp",
        web_fs,
        "esp32BaseOwnsPath",
        "Web FS must recognize Esp32Base-owned paths generically",
    ),
    (
        "src/web/internal/WebFs.cpp",
        web_fs,
        "esp32BaseOwnsPath",
        "Web FS must recognize Esp32Base-owned paths for display",
    ),
    (
        "src/web/internal/WebFs.cpp",
        web_fs,
        "esp32base managed",
        "Web FS must label Esp32Base-managed files",
    ),
]

for path, text, needle, message in checks:
    if needle not in text:
        errors.append(f"{path}: {message}")

for path in [
    "README.md",
    "docs/03_api.md",
    "docs/04_web.md",
    "docs/06_memory_budget.md",
    "docs/07_diagnostics.md",
    "docs/09_release_checklist.md",
    "docs/10_known_limitations.md",
    "CHANGELOG.md",
]:
    text = read(path)
    for needle in [
        "/esp32base/logs/system.log",
        "/esp32base/app-events/events.bin",
    ]:
        if needle not in text:
            errors.append(f"{path}: missing storage namespace marker {needle!r}")

if 'Target is reserved for Esp32Base services' in web_fs:
    errors.append("src/web/internal/WebFs.cpp: generic /esp32base paths must warn in the UI, not reject upload/delete")
for forbidden in ["reserved_path", "owned by an Esp32Base service", "owned by service"]:
    if forbidden in web_fs:
        errors.append(f"src/web/internal/WebFs.cpp: must not keep hard-block namespace wording {forbidden!r}")

for path in ["README.md", "docs/03_api.md", "docs/04_web.md", "CHANGELOG.md"]:
    text = read(path)
    if "Target is reserved for Esp32Base services" in text:
        errors.append(f"{path}: must not document generic /esp32base upload/delete rejection")
    if "普通上传、覆盖或删除基础库管理路径会被拒绝" in text:
        errors.append(f"{path}: must document /esp32base as a warning namespace, not a generic hard block")

for path in [
    "README.md",
    "docs/03_api.md",
    "docs/04_web.md",
    "docs/06_memory_budget.md",
    "docs/07_diagnostics.md",
    "docs/09_release_checklist.md",
    "docs/10_known_limitations.md",
]:
    text = read(path)
    if "/app/events.bin" in text:
        errors.append(f"{path}: must not document /app/events.bin as the current App Events store")
    if "/logs/eb_app.log" in text:
        errors.append(f"{path}: must not document /logs/eb_app.log as the current FileLog path")

if "PATH_BUFFER_SIZE = 96" not in read("src/runtime/Esp32BaseFileLog.h"):
    errors.append("src/runtime/Esp32BaseFileLog.inc: FileLog path buffer must allow project custom paths up to Web FS path length")
if "SEGMENT_PATH_BUFFER_SIZE = PATH_BUFFER_SIZE + 4" not in read("src/runtime/Esp32BaseFileLog.h"):
    errors.append("src/runtime/Esp32BaseFileLog.h: FileLog segment path buffer size must derive from the path buffer")
if "kFileLogSegmentPathMax" not in filelog_source:
    errors.append("src/runtime/Esp32BaseFileLog.inc: FileLog segment path buffers must not stay at fixed 64 bytes")
if "strlen(ESP32BASE_EB_FILELOG_PATH) >= sizeof(g_fileLogPath)" not in filelog_source:
    errors.append("src/runtime/Esp32BaseFileLog.inc: FileLog begin must validate ESP32BASE_EB_FILELOG_PATH length")

if "bool fsJoinPath(" not in web_fs or "bool fsJoinPath(" not in web_internal:
    errors.append("src/web/internal Web modules: fsJoinPath must return bool so truncation is detectable")
for helper_name in [
    "fsJoinPath",
    "fsReadArg",
    "fsReadPathArg",
    "fsBuildUploadPath",
    "fsSendUploadJson",
    "fsUploadTargetIsDirectory",
    "fsUploadFileReadableEnd",
    "fsDownloadFilename",
    "fileLogOwnsPath",
]:
    declaration = extract_declaration(web_internal, helper_name)
    definition = extract_definition(web_fs, helper_name)
    if not declaration:
        errors.append(f"src/web/internal/WebInternal.h: missing declaration for {helper_name}")
    elif not definition:
        errors.append(f"src/web/internal/WebFs.cpp: missing definition for {helper_name}")
    elif declaration != definition:
        errors.append(
            "src/web/internal Web modules: signature mismatch for "
            f"{helper_name}: header {declaration!r} vs source {definition!r}"
        )
if "sendFsPathTooLongRow(" not in web_fs:
    errors.append("src/web/internal/WebFs.cpp: FS tree must render overlong paths without download/delete actions")
if "fsReadPathArg(" not in web_fs:
    errors.append("src/web/internal/WebFs.cpp: request path args must be length-checked before copying")

for line_no, line in enumerate(web_fs.splitlines(), start=1):
    if "fsJoinPath(" not in line or "bool fsJoinPath(" in line:
        continue
    stripped = line.strip()
    handled = (
        stripped.startswith("const bool ") or
        stripped.startswith("bool ") or
        stripped.startswith("if (!fsJoinPath(") or
        stripped.startswith("return fsJoinPath(")
    )
    if not handled:
        errors.append(f"src/web/internal/WebFs.cpp:{line_no}: fsJoinPath return value must be checked")

for forbidden in ["strlcpy(path, g_server.arg", "strlcpy(dir, g_server.arg", "strlcpy(name, g_server.arg"]:
    if forbidden in web_fs:
        errors.append(f"src/web/internal/WebFs.cpp: request args must be length-checked before copying ({forbidden})")

download = function_body(web_fs, "void handleFsDownloadGet()")
delete = function_body(web_fs, "void handleFsDeletePost()")
check = function_body(web_fs, "void handleFsCheckGet()")
upload = function_body(web_fs, "void handleFsUpload()")
walk = function_body(web_fs, "void fsWalkCallback(const char* name")
upload_dirs = function_body(web_fs, "void fsUploadDirOptionCallback(const char* name")
if "fsReadPathArg(\"path\", path, sizeof(path))" not in download:
    errors.append("src/web/internal/WebFs.cpp: download must validate raw path length before copying")
if "fsReadPathArg(\"path\", path, sizeof(path))" not in delete:
    errors.append("src/web/internal/WebFs.cpp: delete must validate raw path length before copying")
if "fsReadArg(\"dir\", dir, sizeof(dir))" not in check or "fsReadArg(\"name\", name, sizeof(name))" not in check:
    errors.append("src/web/internal/WebFs.cpp: upload check must validate raw dir/name length before copying")
if "fsReadArg(\"dir\", dir, sizeof(dir))" not in upload:
    errors.append("src/web/internal/WebFs.cpp: upload start must validate raw dir length before copying")
if "const bool pathOk = fsJoinPath(" not in walk:
    errors.append("src/web/internal/WebFs.cpp: file tree walk must branch on fsJoinPath return value")
if "if (!fsJoinPath(" not in upload_dirs:
    errors.append("src/web/internal/WebFs.cpp: upload directory options must skip overlong joined paths")
if "Some directories are hidden because their paths are too long." not in web_fs and "disabled>path too long" not in upload_dirs:
    errors.append("src/web/internal/WebFs.cpp: upload directory picker must show when overlong directories are hidden")
if 'strlcpy(path, g_server.hasArg("path")' in download + delete:
    errors.append("src/web/internal/WebFs.cpp: download/delete still copy possibly overlong path before validation")
if "char segment[64]" in filelog_owns:
    errors.append("src/web/internal/WebFs.cpp: fileLogOwnsPath must not use a smaller segment buffer than FileLog")
if "char g_fileLogPath[Esp32BaseFileLog::PATH_BUFFER_SIZE] = \"\"" not in filelog_source:
    errors.append("src/runtime/Esp32BaseFileLog.inc: FileLog path buffer must start empty so overlong build flags do not fail compilation")
if "Esp32BaseFileLog::SEGMENT_PATH_BUFFER_SIZE" not in filelog_owns:
    errors.append("src/web/internal/WebFs.cpp: fileLogOwnsPath must use the FileLog segment path buffer size")

if errors:
    for error in errors:
        print(f"FAIL: {error}")
    raise SystemExit(1)

print("Storage namespace checks passed")
