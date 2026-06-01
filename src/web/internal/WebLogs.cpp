#include "../../Esp32BaseProfile.h"

#if ESP32BASE_ENABLE_WEB

#include "WebInternal.h"

namespace esp32base_web {

#if ESP32BASE_ENABLE_FILELOG
uint8_t selectedLogSegment() {
    if (!g_server.hasArg("segment")) {
        return 0;
    }
    const String raw = g_server.arg("segment");
    if (raw.length() == 0 || raw.length() > 3) {
        return 0;
    }
    uint16_t value = 0;
    for (size_t i = 0; i < raw.length(); ++i) {
        const char c = raw.charAt(i);
        if (c < '0' || c > '9') {
            return 0;
        }
        value = static_cast<uint16_t>(value * 10U + static_cast<uint16_t>(c - '0'));
    }
    if (value >= Esp32BaseFileLog::rotateFiles()) {
        return 0;
    }
    return static_cast<uint8_t>(value);
}

void sendRawLogChunk(const char* data, size_t len, void* user) {
    (void)user;
    sendChunk(data, len);
#if ESP32BASE_ENABLE_WATCHDOG
    Esp32BaseWatchdog::feed();
#endif
    yield();
}

void logSegmentTitle(uint8_t index, char* out, size_t len) {
    if (!out || len == 0) {
        return;
    }
    if (index == 0) {
        snprintf(out, len, "current-0");
        return;
    }
    snprintf(out, len, "history-%u", static_cast<unsigned>(index));
}

void sendLogSegmentTabs(uint8_t selected) {
    char title[16];
    char sizeBuf[48];
    char line[160];
    sendChunk("<div class='tabs'>");
    for (uint8_t i = 0; i < Esp32BaseFileLog::rotateFiles(); ++i) {
        logSegmentTitle(i, title, sizeof(title));
        formatReadableBytes(Esp32BaseFileLog::segmentSize(i), sizeBuf, sizeof(sizeBuf));
        snprintf(line, sizeof(line), "<a%s href='/esp32base/logs?segment=%u'>",
                 i == selected ? " class='active'" : "",
                 static_cast<unsigned>(i));
        sendChunk(line);
        sendChunk("<span class='segname'>");
        sendEscapedHtmlChunk(title);
        sendChunk("</span><span class='segsize'>");
        sendEscapedHtmlChunk(sizeBuf);
        sendChunk("</span></a>");
    }
    sendChunk("</div>");
}
#endif

void handleLogsPage() {
    markRequest();
    if (!ensureAuth()) {
        return;
    }
    Esp32BaseWeb::sendHeader(g_builtinLabels[Esp32BaseWeb::BUILTIN_LOGS]);
    Esp32BaseWeb::sendPageTitle(g_builtinLabels[Esp32BaseWeb::BUILTIN_LOGS]);
    if (g_server.hasArg("cleared")) {
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_OK, "System logs cleared");
    } else if (g_server.hasArg("error")) {
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_DANGER, "System logs action failed", g_server.arg("error").c_str());
    }
#if ESP32BASE_ENABLE_FILELOG
    Esp32BaseFileLog::flush();
    char maxBuf[48];
    char totalBuf[48];
    char bufferUsed[48];
    char bufferTotal[48];
    formatReadableBytes(Esp32BaseFileLog::maxBytes(), maxBuf, sizeof(maxBuf));
    formatReadableBytes(static_cast<uint64_t>(Esp32BaseFileLog::maxBytes()) * Esp32BaseFileLog::rotateFiles(), totalBuf, sizeof(totalBuf));
    formatReadableBytes(Esp32BaseFileLog::bufferUsed(), bufferUsed, sizeof(bufferUsed));
    formatReadableBytes(Esp32BaseFileLog::bufferSize(), bufferTotal, sizeof(bufferTotal));
    char line[320];
    sendChunk("<section class='panel logpanel'><div class='tablewrap'><table class='logmeta'><tr><th>System logs</th><td>");
    sendFileLogRuntimeStateTag();
    sendChunk("</td></tr><tr><th>Path</th><td>");
    sendEscapedHtmlChunk(Esp32BaseFileLog::path());
    snprintf(line, sizeof(line), "</td></tr><tr><th>Rotation files</th><td>%u</td></tr><tr><th>Mode</th><td>%s</td></tr><tr><th>Buffer</th><td>%s / %s</td></tr><tr><th>Flush interval</th><td>%lu ms</td></tr><tr><th>Max per file</th><td>%s</td></tr><tr><th>Max total</th><td>%s</td></tr><tr><th>Segments</th><td>",
             static_cast<unsigned>(Esp32BaseFileLog::rotateFiles()),
             Esp32BaseFileLog::modeName(),
             bufferUsed,
             bufferTotal,
             static_cast<unsigned long>(Esp32BaseFileLog::flushIntervalMs()),
             maxBuf,
             totalBuf);
    sendChunk(line);
    for (int8_t i = static_cast<int8_t>(Esp32BaseFileLog::rotateFiles()) - 1; i >= 0; --i) {
        char sizeBuf[48];
        formatReadableBytes(Esp32BaseFileLog::segmentSize(static_cast<uint8_t>(i)), sizeBuf, sizeof(sizeBuf));
        snprintf(line, sizeof(line), "%s%u=%s", i == static_cast<int8_t>(Esp32BaseFileLog::rotateFiles()) - 1 ? "" : ", ", static_cast<unsigned>(i), sizeBuf);
        sendChunk(line);
    }
    const uint8_t selectedSegment = selectedLogSegment();
    sendChunk("</td></tr></table></div>");
    sendFileLogRuntimeNotice();
    sendLogSegmentTabs(selectedSegment);
    snprintf(line, sizeof(line), "<p><a href='/esp32base/logs/raw?segment=%u'>Open raw log</a></p><iframe class='logframe' src='/esp32base/logs/raw?segment=%u'></iframe>",
             static_cast<unsigned>(selectedSegment),
             static_cast<unsigned>(selectedSegment));
    sendChunk(line);
    sendChunk("</section>");
#else
    sendChunk("<section class='panel actionpanel'><h2>System logs unavailable</h2><p class='muted'>System diagnostic logs are not available in this firmware profile.</p></section>");
#endif
    Esp32BaseWeb::sendFooter();
}

void handleLogsRaw() {
    markRequest();
    if (!ensureAuth()) {
        return;
    }
#if ESP32BASE_ENABLE_FILELOG
    Esp32BaseFileLog::flush();
    const uint8_t selectedSegment = selectedLogSegment();
    g_server.sendHeader("X-Content-Type-Options", "nosniff");
    if (!beginResponse(200, "text/plain; charset=utf-8", nullptr)) {
        return;
    }
    if (!Esp32BaseFileLog::streamSegment(selectedSegment, sendRawLogChunk)) {
        sendChunk("log unavailable\n");
    }
    endResponse();
#else
    g_server.send(404, "text/plain", "System logs unavailable");
#endif
}

void handleLogsClear() {
    markRequest();
    if (!ensurePostAllowed("logs_clear")) {
        return;
    }
#if ESP32BASE_ENABLE_FILELOG
    const bool ok = Esp32BaseFileLog::clear();
    redirectSeeOther(ok ? "/esp32base/logs?cleared=1" : "/esp32base/logs?error=clear_failed");
#else
    redirectSeeOther("/esp32base/logs?error=unavailable");
#endif
}

} // namespace esp32base_web

#endif
