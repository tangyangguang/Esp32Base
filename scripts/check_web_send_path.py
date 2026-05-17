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
    errors: list[str] = []

    if "sendResponseContent(" in send_progmem:
        errors.append("sendProgmem() must not bypass the response chunk buffer")
    if re.search(r"char\s+buf\s*\[\s*129\s*\]", send_progmem):
        errors.append("sendProgmem() must not emit fixed 128-byte chunks")
    if "g_chunkBuffer" not in send_progmem or "flushChunkBuffer()" not in send_progmem:
        errors.append("sendProgmem() must reuse the shared chunk buffer and flush path")

    if errors:
        for error in errors:
            print(f"FAIL: {error}")
        return 1
    print("web send path: OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
