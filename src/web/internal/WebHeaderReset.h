#pragma once

namespace esp32base_web {

// WebServer uses an array on older cores and linked nodes on newer cores.
// Select by the actual SDK type, not an unrelated IDF version number.
template <typename Header>
auto resetCollectedHeaderValues(Header* headers, int, int)
    -> decltype(headers->next, void()) {
    for (Header* header = headers; header; header = header->next) {
        header->value = "";
    }
}

template <typename Header>
void resetCollectedHeaderValues(Header* headers, int count, long) {
    if (!headers) {
        return;
    }
    for (int i = 0; i < count; ++i) {
        headers[i].value = "";
    }
}

// Newer WebServer retains response nodes until the next request. Our bounded
// client loop must release them too, including responses rejecting a request.
template <typename Header>
void releaseResponseHeaders(Header*& headers) {
    while (headers) {
        Header* next = headers->next;
        delete headers;
        headers = next;
    }
}

template <typename Headers>
void releaseResponseHeaders(Headers& headers) {
    headers = "";
}

} // namespace esp32base_web
