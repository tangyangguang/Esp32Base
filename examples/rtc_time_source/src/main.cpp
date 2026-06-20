#include <Arduino.h>
#include <Esp32Base.h>
#include <Wire.h>

namespace {
uint32_t g_lastPrintMs = 0;

void printTimeSnapshot() {
    const Esp32BaseTime::Snapshot time = Esp32BaseTime::snapshot();
    char text[32];
    Serial.print("time source=");
    Serial.print(Esp32BaseTime::sourceName(time.source));
    Serial.print(" synced=");
    Serial.print(time.synced ? "yes" : "no");
    Serial.print(" uptimeSec=");
    Serial.print(time.uptimeSec);
    if (Esp32BaseTime::formatTime(text, sizeof(text))) {
        Serial.print(" current=");
        Serial.print(text);
    }
    Serial.print(" rtc=");
    Serial.print(Esp32BaseRtc::driverName());
    Serial.print("/");
    Serial.println(Esp32BaseRtc::statusText());
}
}

void setup() {
    Serial.begin(115200);
    Wire.begin();

    Esp32Base::setFirmwareInfo("rtc_time_source", "1.0.0");
    Esp32BaseRtc::configure(Wire);
    Esp32Base::begin();
    printTimeSnapshot();
}

void loop() {
    Esp32Base::handle();
    const uint32_t now = millis();
    if (now - g_lastPrintMs >= 5000) {
        g_lastPrintMs = now;
        printTimeSnapshot();
    }
    delay(10);
}
