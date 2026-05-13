#include <Arduino.h>
#include <Esp32Base.h>

void setup() {
    Esp32Base::setFirmwareInfo("basic", "1.0.0");
#if ESP32BASE_ENABLE_WEB
    Esp32BaseWeb::setDefaultAuth("admin", "admin");
#endif
    Esp32Base::begin();
}

void loop() {
    Esp32Base::handle();
    delay(10);
}
