#!/usr/bin/env python3
"""Check Web response send path invariants that affect ESP32 page latency."""

from __future__ import annotations

import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
WEB_INC = ROOT / "src" / "web" / "Esp32BaseWeb.inc"


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
    source = WEB_INC.read_text()
    send_progmem = function_body(source, "sendProgmem")
    handle_logs_page = function_body(source, "handleLogsPage")
    handle_logs_raw = function_body(source, "handleLogsRaw")
    errors: list[str] = []

    if "sendResponseContent(" in send_progmem:
        errors.append("sendProgmem() must not bypass the response chunk buffer")
    if re.search(r"char\s+buf\s*\[\s*129\s*\]", send_progmem):
        errors.append("sendProgmem() must not emit fixed 128-byte chunks")
    if "g_chunkBuffer" not in send_progmem or "flushChunkBuffer()" not in send_progmem:
        errors.append("sendProgmem() must reuse the shared chunk buffer and flush path")
    if "sendLogEscapedChunk" in source or "sendLogSegment(" in source:
        errors.append("Logs must not keep the old HTML-escaped inline segment path")
    if "sendLogSegment(" in handle_logs_page or "<pre>" in handle_logs_page:
        errors.append("handleLogsPage() must not inline full log contents into HTML")
    if "/esp32base/logs/raw" not in handle_logs_page or "iframe" not in handle_logs_page:
        errors.append("handleLogsPage() must embed the raw text log endpoint")
    if "text/plain; charset=utf-8" not in handle_logs_raw:
        errors.append("handleLogsRaw() must serve logs as text/plain")
    if "streamSegment" not in handle_logs_raw or "sendRawLogChunk" not in handle_logs_raw:
        errors.append("handleLogsRaw() must stream file log segments through the raw chunk path")
    if 'g_server.on("/esp32base/logs/raw"' not in source:
        errors.append("/esp32base/logs/raw route must be registered")

    if errors:
        for error in errors:
            print(f"FAIL: {error}")
        return 1
    print("web send path: OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
