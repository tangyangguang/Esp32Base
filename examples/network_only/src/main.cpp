#include <Arduino.h>
#include <Esp32Base.h>

void setup() {
    Esp32Base::setFirmwareInfo("network-only", "1.0.0");
    Esp32Base::begin();
    ESP32BASE_LOG_I("example", "network-only example ready");
}

void loop() {
    Esp32Base::handle();
    delay(10);
}
