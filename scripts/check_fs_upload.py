#!/usr/bin/env python3
"""Check built-in FS upload/import invariants."""

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

source = read_web_source()
errors: list[str] = []

checks = {
    "src/web/internal Web modules": [
        "g_server.on(\"/esp32base/fs/check\", HTTP_GET, handleFsCheckGet)",
        "g_server.on(\"/esp32base/fs/upload\", HTTP_POST, handleFsUploadDone, handleFsUpload)",
        "void handleFsCheckGet()",
        "void handleFsUploadDone()",
        "void handleFsUpload()",
        "fsUploadDirectoryExists",
        "fsUploadFilenameValid",
        "fsUploadFileReadableEnd",
        "fs_upload_rejected",
        "fs_upload_completed",
        "appEventsOwnsPath",
        "targetIsAppEvents",
        "Esp32BaseAppEventLog::reload();",
        "Upload file",
        "static_cast<size_t>(written) >= len",
        "static_cast<uint64_t>(actualSize) != uploadBytes",
        "overwrite=1",
        "confirm(",
    ],
    "docs/03_api.md": [
        "/esp32base/fs/check",
        "/esp32base/fs/upload",
        "保留本地文件名",
        "不创建目录",
    ],
    "docs/04_web.md": [
        "/esp32base/fs/check",
        "/esp32base/fs/upload",
        "保留本地文件名",
        "不创建目录",
    ],
    "docs/11_web_ui_baseline.md": [
        "上传保留本地文件名",
        "不创建目录",
        "任何已有目录",
    ],
    "README.md": [
        "/esp32base/fs",
        "上传",
        "保留本地文件名",
    ],
}

for path, needles in checks.items():
    text = source if path == "src/web/internal Web modules" else read(path)
    for needle in needles:
        if needle not in text:
            errors.append(f"{path}: missing {needle!r}")

if "Esp32BaseFs::appendBytes(g_fsUploadPath, upload.buf, upload.currentSize)" not in source:
    errors.append("src/web/internal Web modules: upload chunks must stream through Esp32BaseFs appendBytes")
if "Esp32BaseFs::writeBytes(g_fsUploadPath, nullptr, 0)" not in source:
    errors.append("src/web/internal Web modules: upload must create or truncate the target before streaming chunks")
if "Target is reserved for App Events" in source:
    errors.append("src/web/internal Web modules: App Events store upload should warn/reload, not reject")
if "fsResetUploadState()" not in source:
    errors.append("src/web/internal Web modules: upload request state must be reset after each completed upload request")

web_fs = read("src/web/internal/WebFs.cpp")
upload_done = function_body(web_fs, "void handleFsUploadDone()")
upload_write = function_body(web_fs, "void handleFsUpload()")
if '"No upload received"' not in upload_done:
    errors.append("src/web/internal/WebFs.cpp: upload completion must reject POSTs that did not receive UPLOAD_FILE_START")
if upload_done.find("if (startFailed || uploadError[0])") > upload_done.find("if (!uploadReceived"):
    errors.append("src/web/internal/WebFs.cpp: upload start errors must be reported before the no-upload fallback")
if upload_done.count("fsResetUploadState();") < 4:
    errors.append("src/web/internal/WebFs.cpp: upload completion must clear path/error/bytes flags before every response")
if "Esp32BaseAppEventLog::reload()" in upload_done and "App Events store reload failed" not in upload_done:
    errors.append("src/web/internal/WebFs.cpp: App Events store upload reload failure must be returned to the client")
if "Esp32BaseFileLog::begin()" in upload_done and "FileLog reload failed" not in upload_done:
    errors.append("src/web/internal/WebFs.cpp: FileLog upload reload failure must be returned to the client")
if 'strlcpy(path, g_server.arg' in upload_done + upload_write or 'strlcpy(dir, g_server.arg' in upload_done + upload_write:
    errors.append("src/web/internal/WebFs.cpp: upload path args must not be copied from g_server.arg before length checks")
if "fsUploadReceived(false)" not in read("src/web/internal/WebContext.cpp"):
    errors.append("src/web/internal/WebContext.cpp: fsUploadReceived must be explicitly initialized with the upload state")

for forbidden in [
    "fsUploadPathProtected",
    "disabled>protected",
    "Protected directories are shown but cannot be selected.",
]:
    if forbidden in source:
        errors.append(f"src/web/internal Web modules: forbidden upload restriction marker {forbidden!r}")

for path in ["README.md", "docs/03_api.md", "docs/04_web.md", "docs/11_web_ui_baseline.md"]:
    text = read(path)
    for forbidden in ["受保护", "不能作为上传目标", "不能选择"]:
        if forbidden in text and "/esp32base/fs" in text:
            errors.append(f"{path}: upload docs still contain restriction marker {forbidden!r}")

if errors:
    for error in errors:
        print(f"FAIL: {error}")
    raise SystemExit(1)

print("FS upload checks passed")
