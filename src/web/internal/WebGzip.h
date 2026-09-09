#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace esp32base_web {

inline bool encodingTokenEquals(const char* begin, const char* end, const char* token) {
    while (begin != end && *token) {
        char value = *begin++;
        if (value >= 'A' && value <= 'Z') value += 'a' - 'A';
        if (value != *token++) return false;
    }
    return begin == end && *token == '\0';
}

// A missing header allows any coding; an explicitly empty header allows identity only.
// Explicit gzip takes precedence over wildcard, including q=0. No float parser/heap.
inline bool acceptsGzip(const char* value, bool headerPresent) {
    if (!headerPresent) return true;
    if (!value) return false;
    bool wildcard = false;
    bool gzipSeen = false;
    bool gzipAllowed = false;
    const char* cursor = value;
    while (*cursor) {
        while (*cursor == ' ' || *cursor == '\t' || *cursor == ',') ++cursor;
        const char* begin = cursor;
        while (*cursor && *cursor != ',' && *cursor != ';' && *cursor != ' ' && *cursor != '\t') ++cursor;
        const char* end = cursor;
        while (*cursor == ' ' || *cursor == '\t') ++cursor;
        bool allowed = true;
        if (*cursor == ';') {
            ++cursor;
            while (*cursor == ' ' || *cursor == '\t') ++cursor;
            if (*cursor != 'q' && *cursor != 'Q') allowed = false;
            else {
                ++cursor;
                while (*cursor == ' ' || *cursor == '\t') ++cursor;
                if (*cursor++ != '=') return false;
                while (*cursor == ' ' || *cursor == '\t') ++cursor;
                if (*cursor != '0' && *cursor != '1') allowed = false;
                else {
                    const bool one = *cursor++ == '1';
                    bool positive = one;
                    bool valid = true;
                    if (*cursor == '.') {
                        ++cursor;
                        unsigned digits = 0;
                        while (*cursor >= '0' && *cursor <= '9') {
                            if (++digits > 3 || (one && *cursor != '0')) valid = false;
                            positive |= *cursor++ != '0';
                        }
                    }
                    allowed = valid && positive;
                }
            }
            while (*cursor == ' ' || *cursor == '\t') ++cursor;
        }
        if (*cursor && *cursor != ',') allowed = false;
        if (encodingTokenEquals(begin, end, "gzip")) {
            gzipSeen = true;
            gzipAllowed = allowed;
        } else if (encodingTokenEquals(begin, end, "*")) wildcard = allowed;
        while (*cursor && *cursor != ',') ++cursor;
    }
    return gzipSeen ? gzipAllowed : wildcard;
}

template<class Server>
void sendGzipAsset(Server& server, const char* encoding, bool headerPresent,
                   const char* contentType, const uint8_t* data, size_t length) {
    server.sendHeader("Vary", "Accept-Encoding");
    server.sendHeader("X-Content-Type-Options", "nosniff");
    if (!acceptsGzip(encoding, headerPresent)) {
        server.send(406, "text/plain", "gzip encoding required");
        return;
    }
    server.sendHeader("Cache-Control", "public, max-age=86400");
    server.sendHeader("Content-Encoding", "gzip");
    server.send_P(200, contentType, reinterpret_cast<const char*>(data), length);
}

} // namespace esp32base_web
