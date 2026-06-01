#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    target = ROOT / path
    if not target.exists():
        errors.append(f"{path}: missing file")
        return ""
    return target.read_text(encoding="utf-8")


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
                return text[brace : i + 1]
    return ""


errors = []

long_operation = read("src/runtime/internal/Esp32BaseLongOperation.h")
watchdog = read("src/runtime/Esp32BaseWatchdog.inc")
fs = read("src/runtime/Esp32BaseFs.inc")
webfs = read("src/web/internal/WebFs.cpp")
filelog = read("src/runtime/Esp32BaseFileLog.inc")
app_events = read("src/runtime/Esp32BaseAppEventLog.inc")
demo = read("examples/full_demo/src/main.cpp")

for needle, message in (
    ("class LongOperationScope", "missing RAII long operation scope"),
    ("static void service()", "missing unified long operation service hook"),
    ("Esp32BaseWatchdog::feed();", "long operation service must feed watchdog when enabled"),
    ("yield();", "long operation service must yield"),
):
    if needle not in long_operation:
        errors.append(f"src/runtime/internal/Esp32BaseLongOperation.h: {message}")

for needle, message in (
    ("TaskHandle_t g_longOperationTask", "watchdog long operation state must track the owning task"),
    ("uint8_t g_longOperationDepth", "watchdog long operation state must track nesting depth"),
    ("xTaskGetCurrentTaskHandle()", "watchdog long operation state must use the current FreeRTOS task"),
):
    if needle not in watchdog:
        errors.append(f"src/runtime/Esp32BaseWatchdog.inc: {message}")

for needle, message in (
    ("constexpr size_t kFsIoChunkSize = 512", "FS IO chunk size must be fixed at 512 bytes"),
    ("constexpr size_t kFsServiceEveryBytes = 4096", "FS service threshold must be 4KB"),
    ("writeOpenFileChunked", "FS writes must share a chunked writer"),
    ("readOpenFileChunked", "FS reads must share a chunked reader"),
    ("flushOpenFile", "FS flush must be handled as a long operation"),
    ("Esp32BaseLongOperation::LongOperationScope", "FS operations must use long operation scope"),
):
    if needle not in fs:
        errors.append(f"src/runtime/Esp32BaseFs.inc: {message}")

for signature in (
    "bool writeMode(",
    "bool Esp32BaseFs::writeBytesAt",
    "bool Esp32BaseFs::createFixedFile",
):
    body = function_body(fs, signature)
    if "file.write(data, len)" in body:
        errors.append(f"src/runtime/Esp32BaseFs.inc: {signature} must not use one-shot File.write(data, len)")
    if "writeOpenFileChunked" not in body:
        errors.append(f"src/runtime/Esp32BaseFs.inc: {signature} must use writeOpenFileChunked")

for signature in (
    "bool Esp32BaseFs::readBytes(",
    "bool Esp32BaseFs::readBytesAt",
):
    body = function_body(fs, signature)
    if "readFromOpenFile" in body or ".read(out, maxLen)" in body:
        errors.append(f"src/runtime/Esp32BaseFs.inc: {signature} must use chunked reads")
    if "readOpenFileChunked" not in body:
        errors.append(f"src/runtime/Esp32BaseFs.inc: {signature} must use readOpenFileChunked")

download = function_body(webfs, "void handleFsDownloadGet")
if "LittleFS.open(path, \"r\")" in download or "file.readBytes" in download:
    errors.append("src/web/internal/WebFs.cpp: download must read through Esp32BaseFs instead of raw File reads")
upload = function_body(webfs, "void handleFsUpload()")
if "g_fsUploadFile.write" in upload:
    errors.append("src/web/internal/WebFs.cpp: upload must not write raw File chunks directly")
if "Esp32BaseFs::appendBytes" not in upload:
    errors.append("src/web/internal/WebFs.cpp: upload chunks must flow through Esp32BaseFs chunked writes")

if "beginLongFsOperation()" in filelog or "appEventBeginFsOperation()" in app_events:
    errors.append("FileLog/AppEvents should rely on unified long operation scope, not local watchdog helpers")

for needle, message in (
    ("runFsLargeIoSelfTest()", "full_demo selftest must cover large FS IO"),
    ("large-write-12k.bin", "selftest must cover writeBytes 12KB"),
    ("large-overwrite-8k.bin", "selftest must cover writeBytesAt 8KB"),
):
    if needle not in demo:
        errors.append(f"examples/full_demo/src/main.cpp: {message}")

docs = {
    "README.md": "大块读写会在 Esp32BaseFs 层分块并定期让出调度",
    "docs/03_api.md": "大块读写会在 Esp32BaseFs 层分块并定期让出调度",
    "docs/07_diagnostics.md": "Esp32BaseFs 大块读写",
    "docs/09_release_checklist.md": "writeBytes() / appendBytes() / writeBytesAt() 大块读写",
    "docs/10_known_limitations.md": "写入失败不等于已回滚",
}
for path, needle in docs.items():
    if needle not in read(path):
        errors.append(f"{path}: missing long FS operation documentation marker {needle!r}")

if errors:
    for error in errors:
        print(error)
    raise SystemExit(1)

print("FS long operation checks passed")
