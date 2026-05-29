#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path):
    return (ROOT / path).read_text(encoding="utf-8")


source = read("src/runtime/Esp32BaseFs.inc")
filelog = read("src/runtime/Esp32BaseFileLog.inc")
web = read("src/web/Esp32BaseWeb.inc")
docs = {
    "docs/03_api.md": "底层声明文件仍有剩余内容但读出 0 字节时，`readBytes()` / `readBytesAt()` 返回 false",
    "docs/10_known_limitations.md": "逻辑文件大小不等于内容一定可读",
    "CHANGELOG.md": "FS API 读写失败语义收紧",
}

checks = [
    ("bool readFromOpenFile(", "missing shared read failure guard"),
    ("positionBefore < size && n == 0", "read API must treat zero bytes before EOF as failure"),
    ("file.flush();", "write API must flush before post-write verification"),
    ("verifyFileSize(path, expectedSize)", "write API must verify final file size"),
    ("verifyReadableByte(path, verifyOffset)", "write API must verify written range is readable"),
    ("File file = LittleFS.open(path, \"w\");", "remove API must try truncate fallback after LittleFS remove fails"),
    ("return remainingSize == 0;", "remove API must treat successful zeroing as maintenance recovery"),
]

errors = []
for needle, message in checks:
    if needle not in source:
        errors.append(f"src/runtime/Esp32BaseFs.inc: {message}")

if "Esp32BaseFs::readBytesAt(path, 0, &value, 1, &readLen) && readLen == 1" not in web:
    errors.append("src/web/Esp32BaseWeb.inc: FS tree readability check must use Esp32BaseFs failure semantics")

for needle, message in (
    ("void sendFsUnreadableActions(", "FS management must keep delete available for unreadable files"),
    ("sendFsUnreadableActions(path, manage);", "FS tree must render unreadable delete actions in manage mode"),
    ("sendFsDeleteForm(path);", "FS unreadable action must reuse the normal delete form"),
    ("File deleted or cleared", "FS page success notice must not claim every recovery was a hard delete"),
    ("Esp32BaseFileLog::begin();", "FS delete should let FileLog retry after maintenance frees space"),
):
    if needle not in web:
        errors.append(f"src/web/Esp32BaseWeb.inc: {message}")

for needle, message in (
    ("bool appendCurrentChunk(", "FileLog append must isolate potentially slow FS writes"),
    ("const bool watchdogReleased = beginLongFsOperation();\n    const bool ok = Esp32BaseFs::writeBytes", "FileLog truncate must be protected as a long FS operation"),
    ("void markFileLogFault()", "FileLog must stop repeated writes after FS fault"),
    ("markFileLogFault();\n                return false;", "FileLog write failure must trip runtime fault state"),
):
    if needle not in filelog:
        errors.append(f"src/runtime/Esp32BaseFileLog.inc: {message}")

for path, needle in docs.items():
    if needle not in read(path):
        errors.append(f"{path}: missing FS reliability marker {needle!r}")

if errors:
    for error in errors:
        print(error)
    raise SystemExit(1)

print("FS API reliability checks passed")
