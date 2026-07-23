#include <Arduino.h>
#include <Esp32Base.h>

void setup() {
    Esp32Base::setFirmwareInfo("basic", "1.0.0");
#if ESP32BASE_ENABLE_WEB
    Esp32BaseWeb::setDefaultAuth("admin", "admin");
#endif
    const bool baseReady = Esp32Base::begin();
#if ESP32BASE_ENABLE_OTA && ESP32BASE_OTA_REQUIRE_MARK_VALID
    // This minimal example has no application-specific hardware to verify.
    // Real applications should include their required configuration, storage,
    // sensors, actuators, and tasks in this health decision.
    if (baseReady && Esp32BaseOta::waitingForMarkValid()) {
        Esp32BaseOta::markCurrentValid();
    }
#endif
}

void loop() {
    Esp32Base::handle();
    delay(10);
}
