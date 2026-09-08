#!/usr/bin/env python3
"""Exercise production header lifecycles and client timeouts across SDK layouts."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
MAIN = r'''
#include <cassert>
#include <string>
#include "src/web/internal/WebHeaderReset.h"
#include "src/web/internal/WebClientTimeout.h"
struct OldClient {
    uint32_t seconds = 0;
    int setTimeout(uint32_t value) { seconds = value; return 0; }
};
struct NewClient {
    uint32_t socketMs = 0, streamMs = 0;
    void setTimeout(uint32_t value) { streamMs = value; }
    void setConnectionTimeout(uint32_t value) { socketMs = value; }
};
struct ArrayHeader { std::string key, value; };
struct LinkedHeader { std::string key, value; LinkedHeader* next; };
struct ResponseHeader {
    ResponseHeader* next;
    static int alive;
    ResponseHeader(ResponseHeader* tail) : next(tail) { ++alive; }
    ~ResponseHeader() { --alive; }
};
int ResponseHeader::alive = 0;
int main() {
    OldClient oldClient;
    NewClient newClient;
    esp32base_web::setWebClientTimeoutSeconds(oldClient, 1, 0);
    esp32base_web::setWebClientTimeoutSeconds(newClient, 1, 0);
    assert(oldClient.seconds == 1);
    assert(newClient.socketMs == 1000 && newClient.streamMs == 1000);
    using esp32base_web::resetCollectedHeaderValues;
    ArrayHeader array[] = {{"Authorization", "secret"}, {"Host", "device"},
                           {"sentinel", "unchanged"}};
    resetCollectedHeaderValues(array, 2, 0);
    assert(array[0].value.empty() && array[1].value.empty());
    assert(array[0].key == "Authorization" && array[2].value == "unchanged");
    LinkedHeader tail{"Host", "device", nullptr};
    LinkedHeader head{"Authorization", "secret", &tail};
    resetCollectedHeaderValues(&head, 2, 0);
    assert(head.value.empty() && tail.value.empty());
    assert(head.key == "Authorization" && head.next == &tail);
    resetCollectedHeaderValues(&head, 2, 0); // Repeated request, no stale credentials.
    resetCollectedHeaderValues(static_cast<ArrayHeader*>(nullptr), 0, 0);
    resetCollectedHeaderValues(static_cast<LinkedHeader*>(nullptr), 0, 0);
    using esp32base_web::releaseResponseHeaders;
    for (int i = 0; i < 100; ++i) {
        ResponseHeader* response = new ResponseHeader(new ResponseHeader(nullptr));
        releaseResponseHeaders(response);
        assert(response == nullptr && ResponseHeader::alive == 0);
        releaseResponseHeaders(response);
    }
    std::string response = "HTTP header";
    releaseResponseHeaders(response);
    assert(response.empty());
}
'''
with tempfile.TemporaryDirectory(prefix="esp32base-web-headers-") as directory:
    path = Path(directory)
    source = path / "main.cpp"
    source.write_text(MAIN)
    binary = path / "test"
    subprocess.run([os.environ.get("CXX", "c++"), "-std=c++11", "-Wall", "-Wextra",
                    "-Werror", "-I", str(ROOT), str(source), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("Web SDK checks passed: request arrays/nodes, response release, client timeouts")
