#pragma once

#include <Arduino.h>
#include <stdint.h>
#include <Wire.h>

class Esp32BaseRtc {
public:
    enum Driver : uint8_t {
        DRIVER_DS3231 = 1,
        DRIVER_PCF8563 = 2
    };

    enum Status : uint8_t {
        STATUS_DISABLED = 0,
        STATUS_NOT_STARTED = 1,
        STATUS_OK = 2,
        STATUS_MISSING = 3,
        STATUS_I2C_ERROR = 4,
        STATUS_TIME_INVALID = 5,
        STATUS_CLOCK_STOPPED = 6,
        STATUS_CONFIG_ERROR = 7
    };

    static bool configure(TwoWire& wire, uint8_t address = 0);
    static bool begin();
    static void handle();
    static bool refresh();
    static bool isAvailable();
    static bool isTimeValid();
    static bool readEpoch(uint32_t* epochSec);
    static bool setEpoch(uint32_t epochSec);
    static Status status();
    static const char* statusText();
    static const char* driverName();
    static uint32_t lastEpoch();
    static uint32_t lastSyncUptimeSec();
};
