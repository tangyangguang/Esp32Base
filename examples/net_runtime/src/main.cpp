#include <Arduino.h>
#include <Esp32Base.h>

void setup() {
    Esp32Base::setFirmwareInfo("net-runtime", "0.1.0");
    Esp32Base::setHostname("esp32base-net");
    Esp32Base::begin();
#if ESP32BASE_ENABLE_FILELOG
    Esp32BaseFileLog::enable("/logs/eb_app.log", 32UL * 1024UL, Esp32BaseLog::INFO, 4);
#endif
    ESP32BASE_LOG_I("example", "net runtime example ready");
}

void loop() {
    Esp32Base::handle();
    delay(10);
}
