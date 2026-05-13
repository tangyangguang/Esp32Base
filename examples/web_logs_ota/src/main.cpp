#include <Arduino.h>
#include <Esp32Base.h>

void setup() {
    Esp32Base::setFirmwareInfo("web-logs-ota", "1.0.0");
    Esp32BaseWeb::setDefaultAuth("admin", "admin");
    Esp32Base::begin();
    ESP32BASE_LOG_I("example", "open /esp32base/logs or /esp32base/ota");
}

void loop() {
    Esp32Base::handle();
    delay(10);
}
