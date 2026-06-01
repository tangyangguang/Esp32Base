#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path):
    return (ROOT / path).read_text(encoding="utf-8")


header = read("src/runtime/Esp32BaseFs.h")
source = read("src/runtime/Esp32BaseFs.inc")
demo = read("examples/full_demo/src/main.cpp")
docs = {
    "README.md": "createFixedFile()",
    "docs/03_api.md": "createFixedFile(const char* path, uint32_t size, uint8_t fillByte = 0)",
    "docs/09_release_checklist.md": "createFixedFile() 支持 16KB、32KB、64KB",
    "docs/10_known_limitations.md": "createFixedFile()",
    "CHANGELOG.md": "Esp32BaseFs::createFixedFile()",
}

checks = [
    (header, "static bool createFixedFile(const char* path, uint32_t size, uint8_t fillByte = 0);",
     "src/runtime/Esp32BaseFs.h: missing public createFixedFile API"),
    (source, "bool Esp32BaseFs::createFixedFile(const char* path, uint32_t size, uint8_t fillByte)",
     "src/runtime/Esp32BaseFs.inc: missing createFixedFile implementation"),
    (source, "constexpr size_t kFsIoChunkSize = 512",
     "src/runtime/Esp32BaseFs.inc: fixed file creation must use bounded chunk buffer"),
    (source, "LittleFS.open(path, \"w\")",
     "src/runtime/Esp32BaseFs.inc: fixed file creation must recreate/truncate target when needed"),
    (source, "remaining -= static_cast<uint32_t>(chunkLen);",
     "src/runtime/Esp32BaseFs.inc: fixed file creation must write in chunks"),
    (source, "Esp32BaseLongOperation::LongOperationScope",
     "src/runtime/Esp32BaseFs.inc: fixed file creation must protect long flash operation from task WDT"),
    (source, "Esp32BaseLongOperation::service();",
     "src/runtime/Esp32BaseFs.inc: fixed file creation must service long operations"),
    (source, "verifyFileSize(path, size)",
     "src/runtime/Esp32BaseFs.inc: fixed file creation must verify final size"),
    (source, "verifyByteEquals(path, size - 1U, fillByte)",
     "src/runtime/Esp32BaseFs.inc: fixed file creation must verify tail fill byte after recreate"),
    (source, "appendBytes(path",
     "src/runtime/Esp32BaseFs.inc: createFixedFile must not call appendBytes internally"),
    (demo, "runFixedFileSelfTest()",
     "examples/full_demo/src/main.cpp: missing fixed file selftest hook"),
    (demo, "fixed-file-16k.bin",
     "examples/full_demo/src/main.cpp: selftest must cover 16KB fixed file"),
    (demo, "fixed-file-32k.bin",
     "examples/full_demo/src/main.cpp: selftest must cover 32KB fixed file"),
    (demo, "fixed-file-64k.bin",
     "examples/full_demo/src/main.cpp: selftest must cover 64KB fixed file"),
]

errors = []
for text, needle, message in checks:
    if needle == "appendBytes(path":
        impl_start = text.find("bool Esp32BaseFs::createFixedFile")
        impl_end = text.find("bool Esp32BaseFs::removeFile", impl_start)
        impl = text[impl_start:impl_end]
        if needle in impl:
            errors.append(message)
    elif needle not in text:
        errors.append(message)

for path, needle in docs.items():
    if needle not in read(path):
        errors.append(f"{path}: missing fixed file documentation marker {needle!r}")

if errors:
    for error in errors:
        print(error)
    raise SystemExit(1)

print("FS fixed file checks passed")
