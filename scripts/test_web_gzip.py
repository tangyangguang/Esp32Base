#!/usr/bin/env python3
"""Exercise production gzip negotiation/response and lossless feature-specific CSS."""
from pathlib import Path
import subprocess
import tempfile
from generate_web_css import generated, TARGET
from generate_web_page_assets import generated as page_assets, TARGET as PAGE_ASSETS
ROOT = Path(__file__).resolve().parents[1]
MAIN = r'''
#include <cassert>
#include <map>
#include <string>
#include "src/web/internal/WebGzip.h"
struct Server {
    int status = 0;
    std::map<std::string, std::string> headers;
    std::string body;
    void sendHeader(const char* key, const char* value) { headers[key] = value; }
    void send(int code, const char*, const char* text) { status = code; body = text; }
    void send_P(int code, const char*, const char* data, size_t size) { status = code; body.assign(data, size); }
};
int main() {
    using esp32base_web::acceptsGzip;
    assert(acceptsGzip(nullptr, false));
    for (auto value : {"gzip", "br, gzip, deflate", "GZip; q=0.001", "*", "gzip;q=1.000", "*;q=0, gzip"}) assert(acceptsGzip(value, true));
    for (auto value : {"", "identity", "gzip;q=0", "*;q=1, gzip;q=0.000", "gzip;q=2", "gzip;q=1.1", "gzip;q=0.0001", "gzip;q", "gzip;q=", "gzip;q=no", "xgzip", "gzip;unknown=1"}) assert(!acceptsGzip(value, true));
    const uint8_t bytes[] = {0x1f, 0x8b, 0x00, 0xff};
    Server server;
    esp32base_web::sendGzipAsset(server, "gzip", true, "text/css", bytes, sizeof(bytes));
    assert(server.status == 200 && server.body.size() == 4 && static_cast<unsigned char>(server.body[3]) == 255);
    assert(server.headers["Content-Encoding"] == "gzip");
    assert(server.headers["Vary"] == "Accept-Encoding");
    assert(server.headers["X-Content-Type-Options"] == "nosniff");
    server = {};
    esp32base_web::sendGzipAsset(server, "gzip;q=0", true, "text/css", bytes, sizeof(bytes));
    assert(server.status == 406 && !server.headers.count("Content-Encoding"));
}
'''
assert TARGET.read_text() == generated(), 'Stale generated CSS'
assert PAGE_ASSETS.read_text() == page_assets(), 'Stale generated page assets'
with tempfile.TemporaryDirectory(prefix='esp32base-gzip-') as directory:
    root = Path(directory)
    (root / 'test.cpp').write_text(MAIN)
    subprocess.run(['c++', '-std=c++11', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                    '-I', str(ROOT), str(root / 'test.cpp'), '-o', str(root / 'test')], check=True)
    subprocess.run([str(root / 'test')], check=True)
print('Gzip: negotiation, binary response, rejection, sanitizers and all CSS round-trips passed')
