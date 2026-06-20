#!/usr/bin/env python3
"""Check Web invariants that affect ESP32 page latency and route registration."""

from __future__ import annotations

import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]

WEB_SOURCE_PATHS = [
    "src/web/internal/WebResponse.cpp",
    "src/web/internal/WebLogs.cpp",
    "src/web/Esp32BaseWeb.cpp",
    "src/web/internal/WebInternal.h",
    "src/web/internal/WebContext.h",
    "src/web/internal/WebContext.cpp",
    "src/web/internal/WebAssets.cpp",
    "src/web/internal/WebAuth.cpp",
    "src/web/internal/WebFs.cpp",
    "src/web/internal/WebLayout.cpp",
    "src/web/internal/WebOta.cpp",
    "src/web/internal/WebRouting.cpp",
    "src/web/internal/WebStatus.cpp",
    "src/web/internal/WebTools.cpp",
    "src/web/internal/WebWifi.cpp",
    "src/web/internal/WebAppConfig.cpp",
]

def read_web_source() -> str:
    return "\n".join((ROOT / path).read_text(encoding="utf-8") for path in WEB_SOURCE_PATHS)



def function_body(source: str, name: str) -> str:
    match = re.search(rf"\b(?:void|bool)\s+(?:\w+::)?{re.escape(name)}\s*\([^)]*\)\s*\{{", source)
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
    source = read_web_source()
    web_ota = (ROOT / "src" / "web" / "internal" / "WebOta.cpp").read_text(encoding="utf-8")
    send_progmem = function_body(source, "sendProgmem")
    send_response_content = function_body(source, "sendResponseContent")
    send_raw_chunked_content = function_body(source, "sendRawChunkedContent")
    write_client_bytes = function_body(source, "writeClientBytes")
    send_response_header = function_body(source, "sendResponseHeader")
    end_response = function_body(source, "endResponse")
    send_header = function_body(source, "sendHeader")
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
    if "Esp32BaseFileLog::flush()" in handle_logs_page:
        errors.append("handleLogsPage() must not flush or write from a GET/read-only path")
    if "Esp32BaseFileLog::flush()" in handle_logs_raw:
        errors.append("handleLogsRaw() must not flush or write from a GET/read-only path")
    if "g_server.sendContent(data, len);" in send_response_content:
        after_send = send_response_content.split("g_server.sendContent(data, len);", 1)[1]
        if "responseClientConnected()" in after_send:
            errors.append("sendResponseContent() must not mark a response broken from a post-send connected() check")
        errors.append("sendResponseContent() must not use WebServer::sendContent() for every data chunk")
    if "sendRawChunkedContent(data, len)" not in send_response_content:
        errors.append("sendResponseContent() must use the no-heap raw chunk writer for data chunks")
    if "sendRawChunkedContent(data, len)" in send_response_content:
        before_send, after_send = send_response_content.split("sendRawChunkedContent(data, len)", 1)
        if "feedWatchdogDuringSend()" not in before_send or "feedWatchdogDuringSend()" not in after_send:
            errors.append("sendResponseContent() must feed the watchdog around long chunked writes")
    if "malloc(" in send_raw_chunked_content or "free(" in send_raw_chunked_content:
        errors.append("sendRawChunkedContent() must not allocate heap per chunk")
    if "writeClientBytes(" not in send_raw_chunked_content or ".write(" not in write_client_bytes:
        errors.append("sendRawChunkedContent() must write chunk header/body/footer directly to the client")
    if "while (offset < len)" not in write_client_bytes or "offset += written" not in write_client_bytes:
        errors.append("writeClientBytes() must retry partial WiFiClient.write() progress before failing")
    if re.search(r"if\s*\([^)]*responseClientConnected\(\)[^)]*\)\s*\{\s*g_server\.sendContent\(\"\"\)", end_response, re.S):
        errors.append("endResponse() must always attempt the final empty chunk unless the response is already broken")
    if "/esp32base/logs/raw" not in handle_logs_page or "iframe" not in handle_logs_page:
        errors.append("handleLogsPage() must embed the raw text log endpoint")
    if "text/plain; charset=utf-8" not in handle_logs_raw:
        errors.append("handleLogsRaw() must serve logs as text/plain")
    if "streamSegment" not in handle_logs_raw or "sendRawLogChunk" not in handle_logs_raw:
        errors.append("handleLogsRaw() must stream file log segments through the raw chunk path")
    if 'g_server.on("/esp32base/logs/raw"' not in source:
        errors.append("/esp32base/logs/raw route must be registered")
    if 'g_server.on("/esp32base/ota/raw"' not in source:
        errors.append("/esp32base/ota/raw route must be registered for raw Web OTA")
    if "g_headExtraCallback()" in send_header and "shouldSendHeadExtra()" not in send_header:
        errors.append("sendHeader() must gate app head extra so built-in pages do not inline business CSS")
    if "shouldSendHeadExtra()" in send_header:
        if "isBuiltinWebPath(" not in source:
            errors.append("sendHeader() head extra gating must explicitly skip built-in /esp32base pages")
    if "sendResponseHeader" not in (ROOT / "src" / "web" / "Esp32BaseWeb.h").read_text():
        errors.append("Esp32BaseWeb must expose sendResponseHeader() for cacheable app assets")
    if "validHeaderName(" not in source:
        errors.append("sendResponseHeader() must validate header names before passing them to WebServer")
    if "g_responseActive" not in send_response_header:
        errors.append("sendResponseHeader() must reject headers after a chunked response has started")
    if "g_server.sendHeader(name, value)" not in send_response_header:
        errors.append("sendResponseHeader() must delegate valid headers to WebServer::sendHeader()")
    if "builtinRoutes" in source or "server_registering builtin_routes=" in source:
        errors.append("Web begin() must not maintain a separate hand-counted builtin route total")
    if "function h(n){var u=['B','KB','MB','GB']" in source or "function fsUH(n){var u=['B','KB','MB','GB']" in source:
        errors.append("FS and OTA upload pages must share the built-in ebFmtBytes() JavaScript helper")
    if "function ebFmtBytes(n)" not in source:
        errors.append("Web upload pages must provide one shared ebFmtBytes() JavaScript helper")
    if "#if ESP32BASE_ENABLE_OTA" in web_ota:
        errors.append("src/web/internal/WebOta.cpp is already guarded by WEB && OTA and must not repeat ESP32BASE_ENABLE_OTA guards")

    if errors:
        for error in errors:
            print(f"FAIL: {error}")
        return 1
    print("web send path: OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
