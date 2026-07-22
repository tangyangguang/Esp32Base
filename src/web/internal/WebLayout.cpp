#include "../../Esp32BaseProfile.h"

#if ESP32BASE_ENABLE_WEB

#include "WebInternal.h"

namespace esp32base_web {

void sendLink(const char* path, const char* title, bool paragraph, const char* extraClass = nullptr) {
    sendChunk(paragraph ? "<p><a href='" : "<a href='");
    sendEscapedHtmlChunk(path);
    sendChunk("'");
    if (extraClass && extraClass[0]) {
        sendChunk(" class='");
        sendEscapedHtmlChunk(extraClass);
        sendChunk("'");
    }
    sendChunk(">");
    sendEscapedHtmlChunk(title);
    sendChunk(paragraph ? "</a></p>" : "</a>");
}

void sendNavLink(const char* path, const char* title, bool paragraph, const char* activePath, const char* baseClass = nullptr) {
    char className[24] = "";
    if (baseClass && baseClass[0]) {
        strlcpy(className, baseClass, sizeof(className));
    }
    if (activePath && strcmp(activePath, path) == 0) {
        if (className[0]) {
            strlcat(className, " active", sizeof(className));
        } else {
            strlcpy(className, "active", sizeof(className));
        }
    }
    sendLink(path, title, paragraph, className[0] ? className : nullptr);
}

void sendPaginationLink(const char* label, const Esp32BaseWeb::Pagination& pagination, uint32_t page, bool disabled) {
    if (disabled) {
        sendChunk("<span class='btnlink disabled'>");
        sendEscapedHtmlChunk(label);
        sendChunk("</span>");
        return;
    }
    const char* path = pagination.path ? pagination.path : "/esp32base";
    sendChunk("<a class='btnlink' href='");
    sendEscapedHtmlChunk(path);
    sendChunk(strchr(path, '?') ? "&amp;" : "?");
    if (pagination.query && pagination.query[0]) {
        sendEscapedHtmlChunk(pagination.query);
        sendChunk("&amp;");
    }
    char perValue[16];
    snprintf(perValue, sizeof(perValue), "%lu", static_cast<unsigned long>(pagination.perPage == 0 ? 10 : pagination.perPage));
    sendChunk("per=");
    sendChunk(perValue);
    sendChunk("&amp;");
    char value[16];
    snprintf(value, sizeof(value), "%lu", static_cast<unsigned long>(page));
    sendChunk("page=");
    sendChunk(value);
    sendChunk("'>");
    sendEscapedHtmlChunk(label);
    sendChunk("</a>");
}

void sendPaginationPageNumber(const Esp32BaseWeb::Pagination& pagination, uint32_t page, uint32_t currentPage) {
    char label[12];
    snprintf(label, sizeof(label), "%lu", static_cast<unsigned long>(page));
    if (page == currentPage) {
        sendChunk("<span class='btnlink current' aria-current='page'>");
        sendChunk(label);
        sendChunk("</span>");
        return;
    }
    sendPaginationLink(label, pagination, page, false);
}

void sendPaginationActionPath(const char* path) {
    const char* p = path && path[0] ? path : "/esp32base";
    sendChunk(" action='");
    for (size_t i = 0; p[i] && p[i] != '?'; ++i) {
        char c[2] = {p[i], '\0'};
        sendEscapedHtmlChunk(c);
    }
    sendChunk("'");
}

int8_t queryHexValue(char c) {
    if (c >= '0' && c <= '9') {
        return static_cast<int8_t>(c - '0');
    }
    if (c >= 'a' && c <= 'f') {
        return static_cast<int8_t>(10 + c - 'a');
    }
    if (c >= 'A' && c <= 'F') {
        return static_cast<int8_t>(10 + c - 'A');
    }
    return -1;
}

void decodeQueryComponent(char* value) {
    if (!value) {
        return;
    }
    char* out = value;
    for (char* in = value; *in; ++in) {
        if (*in == '+') {
            *out++ = ' ';
            continue;
        }
        if (*in == '%' && in[1] && in[2]) {
            const int8_t high = queryHexValue(in[1]);
            const int8_t low = queryHexValue(in[2]);
            if (high >= 0 && low >= 0) {
                *out++ = static_cast<char>((high << 4) | low);
                in += 2;
                continue;
            }
        }
        *out++ = *in;
    }
    *out = '\0';
}

void sendHiddenInput(const char* name, const char* value) {
    if (!name || !name[0] || strcmp(name, "page") == 0 || strcmp(name, "per") == 0) {
        return;
    }
    sendChunk("<input type='hidden' name='");
    sendEscapedHtmlChunk(name);
    sendChunk("' value='");
    sendEscapedHtmlChunk(value ? value : "");
    sendChunk("'>");
}

void sendQueryHiddenInputs(const char* query) {
    if (!query || !query[0]) {
        return;
    }
    const char* p = strchr(query, '?');
    p = p ? p + 1 : query;
    while (*p) {
        char name[32] = "";
        char value[80] = "";
        size_t ni = 0;
        while (*p && *p != '=' && *p != '&') {
            if (ni + 1 < sizeof(name)) {
                name[ni++] = *p;
            }
            ++p;
        }
        name[ni] = '\0';
        if (*p == '=') {
            ++p;
            size_t vi = 0;
            while (*p && *p != '&') {
                if (vi + 1 < sizeof(value)) {
                    value[vi++] = *p;
                }
                ++p;
            }
            value[vi] = '\0';
        }
        decodeQueryComponent(name);
        decodeQueryComponent(value);
        sendHiddenInput(name, value);
        if (*p == '&') {
            ++p;
        }
    }
}

void sendInfoRowStart(const char* label) {
    sendChunk("<tr><th>");
    sendEscapedHtmlChunk(label);
    sendChunk("</th><td>");
}

void sendInfoRowEnd() {
    sendChunk("</td></tr>");
}

void sendInfoRow(const char* label, const char* value) {
    sendInfoRowStart(label);
    sendEscapedHtmlChunk(value && value[0] ? value : "-");
    sendInfoRowEnd();
}

void sendSubmetricsStart() {
    sendChunk("<div class='submetrics'>");
}

void sendSubmetric(const char* label, const char* value) {
    sendChunk("<span><b>");
    sendEscapedHtmlChunk(label && label[0] ? label : "-");
    sendChunk("</b><em>");
    sendEscapedHtmlChunk(value && value[0] ? value : "-");
    sendChunk("</em></span>");
}

void sendSubmetricsEnd() {
    sendChunk("</div>");
}

void sendStatusTag(Esp32BaseWeb::UiTone tone, const char* text) {
    sendChunk("<span class='tag");
    sendChunk(uiToneClass(tone));
    sendChunk("'>");
    sendEscapedHtmlChunk(text && text[0] ? text : "-");
    sendChunk("</span>");
}

void sendTaggedInfoRow(const char* label, const char* value, Esp32BaseWeb::UiTone tone) {
    sendInfoRowStart(label);
    sendStatusTag(tone, value);
    sendInfoRowEnd();
}

void sendStatusSectionStart(const char* title) {
    sendChunk("<section class='panel statuspage'><h2>");
    sendEscapedHtmlChunk(title);
    sendChunk("</h2><div class='tablewrap'><table class='kv'>");
}

void sendStatusSectionEnd() {
    sendChunk("</table></div></section>");
}

void sendAppLinks(bool paragraph, const char* activePath) {
    const char* home = configuredHomePath();
    for (uint8_t i = 0; i < ESP32BASE_WEB_MAX_NAV_ITEMS; ++i) {
        if (g_navItems[i].path[0] && (paragraph || strcmp(g_navItems[i].path, home) != 0)) {
            sendNavLink(g_navItems[i].path, g_navItems[i].title, paragraph, activePath);
        }
    }
    for (uint8_t i = 0; i < ESP32BASE_WEB_MAX_ROUTES; ++i) {
        if (!g_routes[i].handler || !g_routes[i].appPage || navPathExists(g_routes[i].path)) {
            continue;
        }
        if (!paragraph && strcmp(g_routes[i].path, home) == 0) {
            continue;
        }
        sendNavLink(g_routes[i].path, g_routes[i].title[0] ? g_routes[i].title : g_routes[i].path, paragraph, activePath);
    }
}

void sendSystemLinks(bool paragraph, const char* activePath = nullptr) {
    sendNavLink("/esp32base/status", g_builtinLabels[Esp32BaseWeb::BUILTIN_HOME], paragraph, activePath);
    sendNavLink("/esp32base/logs", g_builtinLabels[Esp32BaseWeb::BUILTIN_LOGS], paragraph, activePath);
#if ESP32BASE_ENABLE_APP_EVENTS
    sendNavLink("/esp32base/app-events", g_builtinLabels[Esp32BaseWeb::BUILTIN_APP_EVENTS], paragraph, activePath);
#endif
#if ESP32BASE_ENABLE_APP_CONFIG
    sendNavLink("/esp32base/app-config", "App Config", paragraph, activePath);
#endif
    sendNavLink("/esp32base/system", g_builtinLabels[Esp32BaseWeb::BUILTIN_TOOLS], paragraph, activePath);
}

void sendMainNav() {
    const char* activePath = activeNavPath(g_systemNavMode == Esp32BaseWeb::SYSTEM_NAV_TOP);
    const char* brandPath = g_homeMode == Esp32BaseWeb::HOME_ESP32BASE
                                ? "/esp32base/status"
                                : configuredHomePath();
    sendChunk("<nav>");
    sendNavLink(brandPath, g_deviceName, false, activePath, "brand");
    if (g_homeMode != Esp32BaseWeb::HOME_ESP32BASE) {
        sendAppLinks(false, activePath);
    }
    if (g_systemNavMode == Esp32BaseWeb::SYSTEM_NAV_TOP) {
        sendSystemLinks(false, activePath);
    }
    sendChunk("</nav>");
}

void sendSystemNavSection() {
    if (g_systemNavMode == Esp32BaseWeb::SYSTEM_NAV_BOTTOM) {
        sendChunk("<nav>");
        sendSystemLinks(false, nullptr);
        sendChunk("</nav>");
    }
}

void formatReadableBytes(uint64_t bytes, char* out, size_t len) {
    Esp32BaseLog::formatBytes(bytes, out, len);
}

void sendFooterStats() {
    char heap[48];
    char uptime[48];
    formatReadableBytes(Esp32BaseSystem::freeHeap(), heap, sizeof(heap));
    Esp32BaseLog::formatUptime64(Esp32BaseSystem::uptimeMs64(), uptime, sizeof(uptime));
    sendChunk("Free heap: ");
    sendEscapedHtmlChunk(heap);
    sendChunk(" · Up: ");
    sendEscapedHtmlChunk(uptime);
    sendChunk(" · RSSI: ");
    if (Esp32BaseWiFi::isConnected()) {
        char rssi[24];
        snprintf(rssi, sizeof(rssi), "%ld dBm", static_cast<long>(Esp32BaseWiFi::rssi()));
        sendEscapedHtmlChunk(rssi);
    } else {
        sendChunk("-");
    }
}

void sendProgmem(const char* p) {
    if (!p || !g_responseActive || g_responseBroken) {
        return;
    }
    while (true) {
        const size_t space = sizeof(g_chunkBuffer) - 1U - g_chunkUsed;
        if (space == 0) {
            flushChunkBuffer();
            if (g_responseBroken) {
                return;
            }
            continue;
        }

        char* dest = g_chunkBuffer + g_chunkUsed;
        size_t taken = 0;
        while (taken < space) {
            const char c = static_cast<char>(pgm_read_byte(p++));
            if (!c) {
                g_chunkUsed += taken;
                return;
            }
            dest[taken++] = c;
        }
        g_chunkUsed += taken;
    }
}

HTTPMethod toHttpMethod(Esp32BaseWeb::Method method) {
    switch (method) {
        case Esp32BaseWeb::METHOD_POST: return HTTP_POST;
        case Esp32BaseWeb::METHOD_ANY: return HTTP_ANY;
        case Esp32BaseWeb::METHOD_GET:
        default: return HTTP_GET;
    }
}

} // namespace esp32base_web

#endif
