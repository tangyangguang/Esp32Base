#include <Arduino.h>
#include <Esp32Base.h>

void setup() {
    Esp32Base::setFirmwareInfo("basic", "1.0.0");
    Esp32Base::setHostname("esp32base-basic");
#if ESP32BASE_ENABLE_WEB
    Esp32BaseWeb::setDefaultAuth("admin", "admin");
#endif
    Esp32Base::begin();
#if ESP32BASE_ENABLE_FILELOG
    Esp32BaseFileLog::enable("/logs/eb_app.log", 32UL * 1024UL, Esp32BaseLog::INFO, 4);
    ESP32BASE_LOG_I("example", "file log set to INFO for example build");
#endif
}

void loop() {
    Esp32Base::handle();
    delay(10);
}
