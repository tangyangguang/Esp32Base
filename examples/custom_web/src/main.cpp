#include <Arduino.h>

#include <Esp32Base.h>

namespace {

uint32_t g_ledValue = 0;
uint32_t g_apiCalls = 0;

void handleHome() {
    Esp32BaseWeb::sendHtml(
        200,
        "<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>Custom Web</title></head><body><h1>Custom Web</h1>"
        "<p><a href='/api/state'>State API</a></p>"
        "<form method='post' action='/api/led'><button name='value' value='1'>On</button>"
        "<button name='value' value='0'>Off</button></form>"
        "<p><a href='/esp32base'>Esp32Base 管理页</a></p>"
        "</body></html>");
}

void handleState() {
    ++g_apiCalls;
    char json[192];
    snprintf(json, sizeof(json),
             "{\"ok\":true,\"led\":%lu,\"calls\":%lu,\"heap\":%lu,\"ip\":\"%s\"}",
             static_cast<unsigned long>(g_ledValue),
             static_cast<unsigned long>(g_apiCalls),
             static_cast<unsigned long>(Esp32BaseRuntime::freeHeap()),
             Esp32BaseNetwork::ip());
    Esp32BaseWeb::sendJson(200, json);
}

void handleLed() {
    char value[8] = "";
    if (Esp32BaseWeb::getParam("value", value, sizeof(value))) {
        g_ledValue = value[0] == '0' ? 0U : 1U;
    }
    handleState();
}

}  // namespace

void setup() {
    Esp32Base::setFirmwareInfo("custom_web", "0.1.0", __DATE__ " " __TIME__);
    Esp32Base::setHostname("esp32base-custom");
    Esp32BaseWeb::addPage("/", handleHome);
    Esp32BaseWeb::addApi("/api/state", handleState);
    Esp32BaseWeb::addRoute("/api/led", Esp32BaseWeb::METHOD_POST, handleLed);

    if (!Esp32Base::begin()) {
        Serial.printf("Esp32Base begin failed: %s\r\n", Esp32Base::lastError());
    }
}

void loop() {
    Esp32Base::handle();
}
