#!/usr/bin/env python3
"""Test the unchanged production mDNS sources with a failing SDK double."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
MAIN = r"""
#include <cassert>
#include "src/network/Esp32BaseMdns.inc"
uint32_t nowMs = 0;
uint32_t millis() { return nowMs; }
FakeMdns MDNS;
int main() {
    assert(!Esp32BaseMdns::begin());
    assert(MDNS.attempts == 1);
    for (nowMs = 1; nowMs < 5000; ++nowMs) assert(!Esp32BaseMdns::begin());
    assert(MDNS.attempts == 1);
    assert(!Esp32BaseMdns::addHttpService());
    assert(MDNS.services == 0);
    assert(!Esp32BaseMdns::begin());
    assert(MDNS.attempts == 2);
    MDNS.allowBegin = true;
    nowMs = 10000;
    assert(Esp32BaseMdns::begin());
    assert(Esp32BaseMdns::begin());
    assert(MDNS.attempts == 3);
    assert(Esp32BaseMdns::isRunning());
    assert(Esp32BaseMdns::addHttpService());
    assert(MDNS.services == 1);
    Esp32BaseMdns::stop();
    assert(!Esp32BaseMdns::isRunning());
    MDNS.allowBegin = false;
    nowMs = UINT32_MAX - 100;
    assert(!Esp32BaseMdns::begin());
    const auto attempts = MDNS.attempts;
    nowMs += 4999;
    assert(!Esp32BaseMdns::begin());
    assert(MDNS.attempts == attempts);
    ++nowMs;
    assert(!Esp32BaseMdns::begin());
    assert(MDNS.attempts == attempts + 1);
    Esp32BaseMdns::stop();
    MDNS.allowBegin = true;
    assert(Esp32BaseMdns::begin()); // Explicit stop clears retry wait.
}
"""
with tempfile.TemporaryDirectory(prefix="esp32base-mdns-") as directory:
    target = Path(directory)
    files = {
        "Arduino.h": "#pragma once\n#include <stdint.h>\nuint32_t millis();\n",
        "src/Esp32BaseProfile.h": "#pragma once\n#define ESP32BASE_ENABLE_MDNS 1\n",
        "src/Esp32Base.h": '#pragma once\nstruct Esp32Base { static const char* hostname() { return "example"; } };\n',
        "src/core/Esp32BaseLog.h": "#pragma once\n#define ESP32BASE_LOG_I(...) ((void)0)\n#define ESP32BASE_LOG_E(...) ((void)0)\n",
        "ESPmDNS.h": """#pragma once
struct FakeMdns {
    bool allowBegin = false;
    unsigned attempts = 0, services = 0;
    bool begin(const char*) { ++attempts; return allowBegin; }
    void end() {}
    void addService(const char*, const char*, unsigned) { ++services; }
};
extern FakeMdns MDNS;
""",
        "main.cpp": MAIN,
    }
    for name in ("src/network/Esp32BaseMdns.inc", "src/network/Esp32BaseMdns.h"):
        files[name] = (ROOT / name).read_text()
    for name, contents in files.items():
        path = target / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(contents)
    executable = target / "test"
    subprocess.run([os.environ.get("CXX", "c++"), "-std=c++11", "-Wall", "-Wextra", "-Werror",
                    "-I", str(target), str(target / "main.cpp"), "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
print("mDNS retry, recovery, idempotence and clock-wrap tests passed")
