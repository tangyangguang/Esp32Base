#!/usr/bin/env python3
"""Exercise the production response writer with a slow/disconnecting client."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
MAIN = r'''
#include <cassert>
#include <cstdint>
#include <algorithm>
#include "src/web/internal/WebWrite.h"
uint32_t nowMs = 0;
uint32_t millis() { return nowMs; }
void delay(unsigned long n) { nowMs += n; }
void yield() {}
struct Client {
    bool online = true;
    size_t limit = 2;
    uint32_t cost = 0;
    unsigned calls = 0;
    bool connected() { return online; }
    size_t write(const uint8_t*, size_t n) { ++calls; nowMs += cost; return std::min(limit, n); }
};
int main() {
    const char data[] = "abcdefgh";
    auto feed = []() {};
    Client c;
    assert(esp32base_web::writeResponseBytes(c, data, 8, 0, 30, feed));
    assert(c.calls == 4);
    c = {}; c.limit = 0;
    assert(!esp32base_web::writeResponseBytes(c, data, 8, 0, 30, feed));
    assert(c.calls == 3);
    c = {}; c.cost = 10; nowMs = 0;
    assert(!esp32base_web::writeResponseBytes(c, data, 8, 0, 30, feed));
    assert(c.calls == 3); // Progress does not reset the whole-response deadline.
    c = {}; c.cost = 10; nowMs = 0;
    assert(esp32base_web::writeResponseBytes(c, data, 2, 0, 30, feed));
    assert(esp32base_web::writeResponseBytes(c, data, 2, 0, 30, feed));
    assert(!esp32base_web::writeResponseBytes(c, data, 2, 0, 30, feed));
    c = {}; c.online = false; nowMs = 0;
    assert(!esp32base_web::writeResponseBytes(c, data, 8, 0, 30, feed));
    assert(c.calls == 0);
    c = {}; c.cost = 4; nowMs = UINT32_MAX - 5;
    assert(esp32base_web::writeResponseBytes(c, data, 4, UINT32_MAX - 5, 30, feed));
    c = {}; c.cost = 31; nowMs = 0;
    assert(!esp32base_web::writeResponseBytes(c, data, 2, 0, 30, feed));
}
'''
with tempfile.TemporaryDirectory(prefix='esp32base-web-write-') as directory:
    root = Path(directory)
    (root / 'Arduino.h').write_text('#pragma once\n#include <stdint.h>\nuint32_t millis();\nvoid delay(unsigned long);\nvoid yield();\n')
    (root / 'test.cpp').write_text(MAIN)
    subprocess.run(['c++', '-std=c++11', '-Wall', '-Wextra', '-Werror', '-I', str(root),
                    '-I', str(ROOT), str(root / 'test.cpp'), '-o', str(root / 'test')], check=True)
    subprocess.run([str(root / 'test')], check=True)
    print('Production Web writer: partial/zero/slow writes, disconnect and rollover passed')
