#!/usr/bin/env python3
"""Check that the default /esp32base status page avoids slow flash/FS work."""

from __future__ import annotations

import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
STATUS = (ROOT / "src/web/internal/WebStatus.cpp").read_text(encoding="utf-8")


def function_body(source: str, name: str) -> str:
    match = re.search(rf"\bvoid\s+{re.escape(name)}\s*\([^)]*\)\s*\{{", source)
    if not match:
        raise SystemExit(f"error: {name}() not found")
    depth = 1
    pos = match.end()
    while pos < len(source) and depth:
        char = source[pos]
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
        pos += 1
    if depth:
        raise SystemExit(f"error: {name}() body is not balanced")
    return source[match.end() : pos - 1]


def main() -> int:
    handle_root = function_body(STATUS, "handleRoot")
    send_fs_quick = function_body((ROOT / "src/web/internal/WebFs.cpp").read_text(encoding="utf-8"), "sendFsQuickSummaryRows")
    errors: list[str] = []

    forbidden_root_calls = [
        ("ESP.getSketchSize()", "Status fast path must not verify the running image to get sketch size"),
        ("esp_ota_get_app_elf_sha256", "Status fast path must not compute or format running ELF SHA256"),
        ("scanFs(", "Status fast path must not scan the LittleFS file tree"),
        ("Esp32BaseFileLog::segmentSize", "Status fast path must not open FileLog segments"),
    ]
    for needle, message in forbidden_root_calls:
        if needle in handle_root:
            errors.append(message)

    if "sendPartitionTable();" not in handle_root or not re.search(r"if\s*\(\s*details\s*\)\s*\{\s*sendPartitionTable\(\);", handle_root):
        errors.append("Partition Table must be behind explicit details mode, not always sent on Status")

    if send_fs_quick.count("Esp32BaseFs::usedBytes()") + send_fs_quick.count("Esp32BaseFs::totalBytes()") > 0:
        errors.append("FS quick summary must use one storageInfo call instead of repeated esp_littlefs_info calls")

    if errors:
        for error in errors:
            print(f"FAIL: {error}")
        return 1
    print("web status fast path: OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
