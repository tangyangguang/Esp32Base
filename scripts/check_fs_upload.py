#!/usr/bin/env python3
"""Check built-in FS upload/import invariants."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


source = read("src/web/Esp32BaseWeb.inc")
errors: list[str] = []

checks = {
    "src/web/Esp32BaseWeb.inc": [
        "g_server.on(\"/esp32base/fs/check\", HTTP_GET, handleFsCheckGet)",
        "g_server.on(\"/esp32base/fs/upload\", HTTP_POST, handleFsUploadDone, handleFsUpload)",
        "void handleFsCheckGet()",
        "void handleFsUploadDone()",
        "void handleFsUpload()",
        "fsUploadPathProtected",
        "fsUploadDirectoryExists",
        "fsUploadFilenameValid",
        "fs_upload_rejected",
        "fs_upload_completed",
        "Upload file",
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
        "/logs",
    ],
    "README.md": [
        "/esp32base/fs",
        "上传",
        "保留本地文件名",
    ],
}

for path, needles in checks.items():
    text = source if path == "src/web/Esp32BaseWeb.inc" else read(path)
    for needle in needles:
        if needle not in text:
            errors.append(f"{path}: missing {needle!r}")

if "Esp32BaseFs::writeBytes(" in source:
    errors.append("src/web/Esp32BaseWeb.inc: upload should stream through LittleFS File, not buffer full files")

if errors:
    for error in errors:
        print(f"FAIL: {error}")
    raise SystemExit(1)

print("FS upload checks passed")
