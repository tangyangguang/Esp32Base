#ifndef ESP32_BASE_WEB_H
#define ESP32_BASE_WEB_H

#include <stddef.h>
#include <stdint.h>

#ifndef ESP32BASE_WEB_MAX_APP_ROUTES
#define ESP32BASE_WEB_MAX_APP_ROUTES 16
#endif

#ifndef ESP32BASE_WEB_PATH_LEN
#define ESP32BASE_WEB_PATH_LEN 48
#endif

#ifndef ESP32BASE_WEB_AUTH_USER
#define ESP32BASE_WEB_AUTH_USER "admin"
#endif

#ifndef ESP32BASE_WEB_AUTH_PASS
#define ESP32BASE_WEB_AUTH_PASS "admin123"
#endif

using Esp32BaseWebHandler = void (*)();

class Esp32BaseWeb {
public:
    enum Method : uint8_t {
        METHOD_GET = 0,
        METHOD_POST,
        METHOD_ANY
    };

    static bool begin();
    static void handle();
    static bool isReady();

    static void setAuth(const char* user, const char* pass);
    static void setAuthEnabled(bool enabled);

    static bool addPage(const char* path, Esp32BaseWebHandler handler);
    static bool addApi(const char* path, Esp32BaseWebHandler handler);
    static bool addRoute(const char* path, Method method, Esp32BaseWebHandler handler);

    static bool hasParam(const char* name);
    static bool getParam(const char* name, char* out, size_t len);
    static bool getRequestBody(char* out, size_t len);

    static void sendText(int code, const char* text);
    static void sendJson(int code, const char* json);
    static void sendHtml(int code, const char* html);
    static void sendContent(const char* text);
    static void sendContentP(const char* progmemText);
    static void send404();

    static uint8_t routeCount();
    static uint8_t routeCapacity();
    static void logConfig();

private:
    struct Route {
        bool used;
        Method method;
        char path[ESP32BASE_WEB_PATH_LEN];
        Esp32BaseWebHandler handler;
    };

    static bool addRouteInternal(const char* path, Method method, Esp32BaseWebHandler handler);
    static void registerBuiltInRoutes();
    static bool dispatchCustomRoute();
    static bool ensureAuthorized();
    static void copyText(const char* in, char* out, size_t len);
    static bool methodMatches(Method routeMethod);

    static void handleIndex();
    static void handleStatusApi();
    static void handleWifiPage();
    static void handleWifiPost();
    static void handleWifiApi();
    static void handleOtaPage();
    static void handleOtaPost();
    static void handleOtaUpload();
    static void handleReboot();
    static void handleNotFound();

    static bool _ready;
    static bool _authEnabled;
    static char _authUser[32];
    static char _authPass[32];
    static Route _routes[ESP32BASE_WEB_MAX_APP_ROUTES];
};

#endif
