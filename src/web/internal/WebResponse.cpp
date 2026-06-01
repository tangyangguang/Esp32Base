#include "../../Esp32BaseProfile.h"

#if ESP32BASE_ENABLE_WEB

#include "WebInternal.h"

namespace esp32base_web {

bool sendResponseHeader(const char* name, const char* value) {
    if (!g_requestContextActive) {
        ESP32BASE_LOG_W("web", "send_response_header outside request");
        return false;
    }
    if (g_responseActive) {
        ESP32BASE_LOG_W("web", "send_response_header after response started");
        return false;
    }
    if (!validHeaderName(name, 63)) {
        ESP32BASE_LOG_W("web", "send_response_header invalid name");
        return false;
    }
    if (!validHeaderValue(value, 127)) {
        ESP32BASE_LOG_W("web", "send_response_header invalid value");
        return false;
    }
    g_server.sendHeader(name, value);
    return true;
}

bool beginResponse(int code, const char* contentType, const char* filename) {
    if (!g_requestContextActive) {
        ESP32BASE_LOG_W("web", "begin_response outside request");
        return false;
    }
    if (!validHeaderValue(contentType, 63)) {
        ESP32BASE_LOG_W("web", "begin_response invalid content_type");
        return false;
    }
    if (filename && filename[0]) {
        if (!validDownloadFilename(filename)) {
            ESP32BASE_LOG_W("web", "begin_response invalid filename");
            return false;
        }
        char disposition[96];
        strlcpy(disposition, "attachment; filename=\"", sizeof(disposition));
        if (!appendQuotedHeaderValue(disposition, sizeof(disposition), filename) ||
            strlcat(disposition, "\"", sizeof(disposition)) >= sizeof(disposition)) {
            ESP32BASE_LOG_W("web", "begin_response filename too long");
            return false;
        }
        g_server.sendHeader("Content-Disposition", disposition);
    }
    g_chunkUsed = 0;
    g_responseActive = true;
    g_responseBroken = false;
    g_server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    g_server.send(code, contentType, "");
    yield();
    return true;
}

bool responseClientConnected() {
    return g_server.client().connected();
}

void feedWatchdogDuringSend() {
#if ESP32BASE_ENABLE_WATCHDOG
    Esp32BaseWatchdog::feed();
#endif
}

bool writeClientBytes(WiFiClient& client, const char* data, size_t len) {
    if (len == 0) {
        return true;
    }
    size_t offset = 0;
    uint8_t zeroProgress = 0;
    while (offset < len) {
        feedWatchdogDuringSend();
        const size_t written = client.write(reinterpret_cast<const uint8_t*>(data + offset), len - offset);
        feedWatchdogDuringSend();
        if (written == 0) {
            if (!client.connected() || ++zeroProgress >= 3) {
                return false;
            }
            delay(1);
            continue;
        }
        offset += written;
        zeroProgress = 0;
        yield();
    }
    return true;
}

bool sendRawChunkedContent(const char* data, size_t len) {
    if (!data && len > 0) {
        return false;
    }
    char header[18];
    const int headerLen = snprintf(header, sizeof(header), "%lx\r\n", static_cast<unsigned long>(len));
    if (headerLen <= 0 || static_cast<size_t>(headerLen) >= sizeof(header)) {
        return false;
    }
    WiFiClient client = g_server.client();
    return writeClientBytes(client, header, static_cast<size_t>(headerLen)) &&
           writeClientBytes(client, data, len) &&
           writeClientBytes(client, "\r\n", 2);
}

void markResponseClientDisconnected() {
    if (!g_responseBroken) {
        g_responseBroken = true;
        ESP32BASE_LOG_W("web", "response_client_disconnected uri=%s", g_activeUri[0] ? g_activeUri : "-");
    }
}

bool sendResponseContent(const char* data, size_t len) {
    if (g_responseBroken) {
        return false;
    }
    if (!responseClientConnected()) {
        markResponseClientDisconnected();
        return false;
    }
    feedWatchdogDuringSend();
    if (!sendRawChunkedContent(data, len)) {
        markResponseClientDisconnected();
        return false;
    }
    feedWatchdogDuringSend();
    yield();
    return true;
}

void flushChunkBuffer() {
    if (g_chunkUsed == 0) {
        return;
    }
    if (g_responseBroken) {
        g_chunkUsed = 0;
        return;
    }
    if (!responseClientConnected()) {
        markResponseClientDisconnected();
        g_chunkUsed = 0;
        return;
    }
    g_chunkBuffer[g_chunkUsed] = '\0';
    sendResponseContent(g_chunkBuffer, g_chunkUsed);
    g_chunkUsed = 0;
}

void sendChunk(const char* data, size_t len) {
    if (!data || len == 0) {
        return;
    }
    if (!g_responseActive) {
        ESP32BASE_LOG_W("web", "send_chunk outside response");
        return;
    }
    if (g_responseBroken) {
        return;
    }
    while (len > 0) {
        const size_t space = sizeof(g_chunkBuffer) - 1U - g_chunkUsed;
        if (space == 0) {
            flushChunkBuffer();
            if (g_responseBroken) {
                return;
            }
            continue;
        }
        size_t take = len;
        if (take > space) {
            take = space;
        }
        memcpy(g_chunkBuffer + g_chunkUsed, data, take);
        g_chunkUsed += take;
        data += take;
        len -= take;
    }
}

void sendChunk(const char* text) {
    if (!text) {
        return;
    }
    sendChunk(text, strlen(text));
}

void endResponse() {
    if (!g_responseActive) {
        ESP32BASE_LOG_W("web", "end_response outside response");
        return;
    }
    flushChunkBuffer();
    if (!g_responseBroken) {
        g_server.sendContent("");
        yield();
    }
    g_responseActive = false;
    g_responseBroken = false;
}

void markRequest() {
    const Esp32BaseWeb::Method method = fromHttpMethod(g_server.method());
    g_lastRequestMethod = method;
    g_requestContextActive = true;
    g_currentMethod = method;
    strlcpy(g_activeUri, g_server.uri().c_str(), sizeof(g_activeUri));
}

void redirectSeeOther(const char* url) {
    g_server.sendHeader("Location", url, true);
    g_server.sendHeader("Cache-Control", "no-store");
    g_server.send(303, "text/plain", "");
}

void sendUintChunk(uint64_t value) {
    char buf[24];
    snprintf(buf, sizeof(buf), "%llu", static_cast<unsigned long long>(value));
    sendChunk(buf);
}

void sendIntChunk(int value) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", value);
    sendChunk(buf);
}

void sendEscapedJsonChunk(const char* text) {
    if (!text) {
        return;
    }
    for (const char* p = text; *p; ++p) {
        switch (*p) {
            case '"': sendChunk("\\\""); break;
            case '\\': sendChunk("\\\\"); break;
            case '\n': sendChunk("\\n"); break;
            case '\r': sendChunk("\\r"); break;
            case '\t': sendChunk("\\t"); break;
            default:
                if (static_cast<uint8_t>(*p) < 0x20) {
                    char buf[7];
                    snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned>(*p));
                    sendChunk(buf);
                } else {
                    char one[2] = {*p, '\0'};
                    sendChunk(one);
                }
                break;
        }
    }
}

void sendBytesJsonChunk(uint32_t bytes) {
    char human[48];
    Esp32BaseLog::formatBytes(bytes, human, sizeof(human));
    sendChunk("{\"bytes\":");
    sendUintChunk(bytes);
    sendChunk(",\"human\":\"");
    sendEscapedJsonChunk(human);
    sendChunk("\"}");
}

void sendEscapedHtmlChunk(const char* text) {
    if (!text) {
        return;
    }
    for (const char* p = text; *p; ++p) {
        switch (*p) {
            case '&': sendChunk("&amp;"); break;
            case '<': sendChunk("&lt;"); break;
            case '>': sendChunk("&gt;"); break;
            case '"': sendChunk("&quot;"); break;
            case '\'': sendChunk("&#39;"); break;
            default: {
                char one[2] = {*p, '\0'};
                sendChunk(one);
                break;
            }
        }
    }
}

} // namespace esp32base_web

#endif
