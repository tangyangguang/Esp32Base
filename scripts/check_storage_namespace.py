#!/usr/bin/env python3
"""Check Esp32Base-owned LittleFS namespace boundaries."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def function_body(text: str, signature: str) -> str:
    start = text.find(signature)
    if start < 0:
        return ""
    brace = text.find("{", start)
    if brace < 0:
        return ""
    depth = 0
    for i in range(brace, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return text[brace:i + 1]
    return text[brace:]


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
if "sendFsPathTooLongRow(" not in web_fs:
    errors.append("src/web/internal/WebFs.cpp: FS tree must render overlong paths without download/delete actions")
if "fsReadPathArg(" not in web_fs:
    errors.append("src/web/internal/WebFs.cpp: request path args must be length-checked before copying")

download = function_body(web_fs, "void handleFsDownloadGet()")
delete = function_body(web_fs, "void handleFsDeletePost()")
if "fsReadPathArg(\"path\", path, sizeof(path))" not in download:
    errors.append("src/web/internal/WebFs.cpp: download must validate raw path length before copying")
if "fsReadPathArg(\"path\", path, sizeof(path))" not in delete:
    errors.append("src/web/internal/WebFs.cpp: delete must validate raw path length before copying")
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
