#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path):
    return (ROOT / path).read_text(encoding="utf-8")


header = read("src/runtime/Esp32BaseFs.h")
source = read("src/runtime/Esp32BaseFs.inc")
web = read("src/web/internal/WebFs.cpp")
internal = read("src/web/internal/WebInternal.h")

checks = [
    (
        header,
        "struct EntryInfo",
        "src/runtime/Esp32BaseFs.h: missing EntryInfo metadata struct",
    ),
    (
        header,
        "uint32_t modifiedEpoch;",
        "src/runtime/Esp32BaseFs.h: EntryInfo must expose last modified epoch",
    ),
    (
        header,
        "using ListInfoCallback = void (*)(const EntryInfo& entry, void* user);",
        "src/runtime/Esp32BaseFs.h: missing listDirInfo callback type",
    ),
    (
        header,
        "static bool listDirInfo(const char* path, ListInfoCallback cb, void* user = nullptr);",
        "src/runtime/Esp32BaseFs.h: missing public listDirInfo API",
    ),
    (
        source,
        "bool Esp32BaseFs::listDirInfo(const char* path, ListInfoCallback cb, void* user)",
        "src/runtime/Esp32BaseFs.inc: missing listDirInfo implementation",
    ),
    (
        source,
        "file.getLastWrite()",
        "src/runtime/Esp32BaseFs.inc: listDirInfo must read File::getLastWrite() during enumeration",
    ),
    (
        source,
        "listDirAdapterCallback",
        "src/runtime/Esp32BaseFs.inc: listDir should delegate through metadata enumeration",
    ),
    (
        web,
        "<th>Last modified</th><th>Status</th><th>Action</th>",
        "src/web/internal/WebFs.cpp: file tree must show Last modified and Status columns",
    ),
    (
        web,
        "fsFormatModifiedTime(",
        "src/web/internal/WebFs.cpp: file tree rows must format last modified time",
    ),
    (
        web,
        'sendStatusTag(Esp32BaseWeb::UI_OK, "ok");',
        "src/web/internal/WebFs.cpp: file tree must expose ok status separately from actions",
    ),
    (
        internal,
        "void sendFsTreeRow(const char* path, size_t size, bool isDir, uint32_t modifiedEpoch, bool manage);",
        "src/web/internal/WebInternal.h: sendFsTreeRow declaration must include modifiedEpoch",
    ),
]

docs = {
    "README.md": "Last modified",
    "docs/03_api.md": "listDirInfo",
    "docs/04_web.md": "Last modified",
    "docs/09_release_checklist.md": "Last modified",
}


errors = []
for text, needle, message in checks:
    if needle not in text:
        errors.append(message)

for path, needle in docs.items():
    if needle not in read(path):
        errors.append(f"{path}: missing FS file metadata documentation marker {needle!r}")

if errors:
    for error in errors:
        print(error)
    raise SystemExit(1)

print("FS file metadata checks passed")
