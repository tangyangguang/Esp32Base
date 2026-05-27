#pragma once

#include <Arduino.h>
#include <stdint.h>

#ifndef ESP32BASE_WEB_MAX_ROUTES
#if defined(CONFIG_IDF_TARGET_ESP32C3)
#define ESP32BASE_WEB_MAX_ROUTES 12
#else
#define ESP32BASE_WEB_MAX_ROUTES 16
#endif
#endif

#ifndef ESP32BASE_WEB_MAX_NAV_ITEMS
#if defined(CONFIG_IDF_TARGET_ESP32C3)
#define ESP32BASE_WEB_MAX_NAV_ITEMS 8
#else
#define ESP32BASE_WEB_MAX_NAV_ITEMS 12
#endif
#endif

class Esp32BaseWeb {
public:
    static constexpr const char* EVENT_READY = "web.ready";
    static constexpr const char* EVENT_STOPPED = "web.stopped";

    enum Method : uint8_t {
        METHOD_UNKNOWN,
        METHOD_GET,
        METHOD_POST,
        METHOD_ANY
    };

    enum HomeMode : uint8_t {
        HOME_ESP32BASE,
        HOME_APP,
        HOME_COMBINED
    };

    enum SystemNavMode : uint8_t {
        SYSTEM_NAV_TOP,
        SYSTEM_NAV_BOTTOM,
        SYSTEM_NAV_SECTION
    };

    enum BuiltinPage : uint8_t {
        BUILTIN_HOME,
        BUILTIN_WIFI,
        BUILTIN_OTA,
        BUILTIN_LOGS,
        BUILTIN_TOOLS,
        BUILTIN_SYSTEM,
        BUILTIN_AUTH
    };

    enum UiTone : uint8_t {
        UI_NEUTRAL,
        UI_OK,
        UI_WARN,
        UI_DANGER,
        UI_INFO
    };

    struct ResultNotice {
        const char* param;
        const char* value;
        UiTone tone;
        const char* title;
        const char* message;
    };

    struct Pagination {
        const char* path;
        const char* query;
        uint32_t page;
        uint32_t perPage;
        uint32_t total;
    };

    using Handler = void (*)();

    static bool begin();
    static void handle();
    static bool isReady();

    static void setDefaultAuth(const char* user, const char* pass);
    static const char* authUser();
    static const char* authPassword();
    static bool isAuthEnabled();
    static void setAuthEnabled(bool enabled);
    static bool checkAuth();
    static bool verifyAuth();
    static bool verifyAuth(const char* user, const char* pass);
    static bool saveAuth(const char* user, const char* pass);
    static bool resetAuth();

    static bool addRoute(const char* path, Method method, Handler handler);
    static bool addPage(const char* path, const char* title, Handler handler);
    static bool addApi(const char* path, Handler handler);
    static bool addNavItem(const char* path, const char* title);

    static bool setDeviceName(const char* name);
    static bool setHomePath(const char* path);
    static void setHomeMode(HomeMode mode);
    static void setSystemNavMode(SystemNavMode mode);
    static bool setBuiltinLabel(BuiltinPage page, const char* label);
    static void setHeadExtraCallback(Handler handler);

    static Method currentMethod();
    static bool isMethod(Method method);
    static const char* currentMethodName();

    static bool hasParam(const char* name);
    static bool getParam(const char* name, char* out, size_t len);
    static bool getRequestBody(char* out, size_t len);

    static void sendHeader(const char* title = nullptr);
    static void sendFooter();
    static void sendPageTitle(const char* title, const char* subtitle = nullptr);
    static void beginPanel(const char* title = nullptr);
    static void endPanel();
    static void sendNotice(UiTone tone, const char* title, const char* message = nullptr);
    static void sendResultNotice(const ResultNotice* notices, uint8_t count);
    static void beginMetricGrid();
    static void sendMetric(const char* label, const char* value, const char* help = nullptr);
    static void endMetricGrid();
    static void sendInfoRowCompact(const char* title, const char* help, const char* value = nullptr, const char* actionHtml = nullptr);
    static void sendPagination(const Pagination& pagination);
    static bool beginResponse(int code, const char* contentType, const char* filename = nullptr);
    static bool beginText(int code);
    static bool beginCsv(int code, const char* filename = nullptr);
    static void endResponse();
    static void sendChunk(const char* text);
    static void sendBytes(const uint8_t* data, size_t len);
    static void writeHtmlEscaped(const char* text);
    static void writeCsvEscaped(const char* text);

    static void sendText(int code, const char* text);
    static void sendHtml(int code, const char* html);
    static void sendJson(int code, const char* json);
    static void redirectSeeOther(const char* url);
    static void beginJson(int code);
    static void writeJsonEscaped(const char* text);
    static void endJson();
};
