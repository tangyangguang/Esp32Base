#include <Arduino.h>
#include <Esp32Base.h>

#if ESP32BASE_PROFILE == ESP32BASE_PROFILE_MINIMAL
  #if ESP32BASE_ENABLE_BUS || ESP32BASE_ENABLE_WATCHDOG || ESP32BASE_ENABLE_SLEEP || \
      ESP32BASE_ENABLE_FS || ESP32BASE_ENABLE_HEALTH || ESP32BASE_ENABLE_WIFI || \
      ESP32BASE_ENABLE_WEB || ESP32BASE_ENABLE_OTA || ESP32BASE_ENABLE_MQTT
  #error "MINIMAL profile contract violated"
  #endif
#elif ESP32BASE_PROFILE == ESP32BASE_PROFILE_OFFLINE
  #if !ESP32BASE_ENABLE_WATCHDOG || !ESP32BASE_ENABLE_FS || !ESP32BASE_ENABLE_FILELOG || \
      !ESP32BASE_ENABLE_HEALTH || ESP32BASE_ENABLE_BUS || ESP32BASE_ENABLE_SLEEP || \
      ESP32BASE_ENABLE_WIFI || ESP32BASE_ENABLE_WEB || ESP32BASE_ENABLE_OTA || ESP32BASE_ENABLE_MQTT
  #error "OFFLINE profile contract violated"
  #endif
#elif ESP32BASE_PROFILE == ESP32BASE_PROFILE_LOCAL
  #if !ESP32BASE_ENABLE_WATCHDOG || !ESP32BASE_ENABLE_FS || !ESP32BASE_ENABLE_FILELOG || \
      !ESP32BASE_ENABLE_HEALTH || !ESP32BASE_ENABLE_WIFI || !ESP32BASE_ENABLE_DNS || \
      !ESP32BASE_ENABLE_NTP || !ESP32BASE_ENABLE_MDNS || !ESP32BASE_ENABLE_WEB || \
      !ESP32BASE_ENABLE_OTA || ESP32BASE_ENABLE_MQTT || ESP32BASE_ENABLE_BUS || ESP32BASE_ENABLE_SLEEP
  #error "LOCAL profile contract violated"
  #endif
#elif ESP32BASE_PROFILE == ESP32BASE_PROFILE_IOT
  #if !ESP32BASE_ENABLE_WATCHDOG || !ESP32BASE_ENABLE_FS || !ESP32BASE_ENABLE_FILELOG || \
      !ESP32BASE_ENABLE_HEALTH || !ESP32BASE_ENABLE_WIFI || !ESP32BASE_ENABLE_DNS || \
      !ESP32BASE_ENABLE_NTP || !ESP32BASE_ENABLE_MDNS || !ESP32BASE_ENABLE_WEB || \
      !ESP32BASE_ENABLE_OTA || !ESP32BASE_ENABLE_MQTT || ESP32BASE_ENABLE_BUS || ESP32BASE_ENABLE_SLEEP
  #error "IOT profile contract violated"
  #endif
#endif

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
