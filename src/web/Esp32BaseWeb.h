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

class Esp32BaseWeb {
public:
    static constexpr const char* EVENT_READY = "web.ready";
    static constexpr const char* EVENT_STOPPED = "web.stopped";

    enum Method : uint8_t {
        METHOD_GET,
        METHOD_POST,
        METHOD_ANY
    };

    using Handler = void (*)();

    static bool begin();
    static void handle();
    static bool isReady();

    static void setAuth(const char* user, const char* pass);
    static bool isAuthEnabled();
    static bool isAuthSetByApplication();
    static void setAuthEnabled(bool enabled);
    static bool checkAuth();
    static bool verifyAuth();

    static bool addRoute(const char* path, Method method, Handler handler);
    static bool addPage(const char* path, Handler handler);
    static bool addApi(const char* path, Handler handler);

    static bool hasParam(const char* name);
    static bool getParam(const char* name, char* out, size_t len);
    static bool getRequestBody(char* out, size_t len);

    static void sendHeader(const char* title = nullptr);
    static void sendFooter();
    static void sendChunk(const char* text);
    static void writeHtmlEscaped(const char* text);

    static void sendText(int code, const char* text);
    static void sendHtml(int code, const char* html);
    static void sendJson(int code, const char* json);
    static void redirectSeeOther(const char* url);
    static void beginJson(int code);
    static void writeJsonEscaped(const char* text);
    static void endJson();
};
