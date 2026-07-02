#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

#ifndef ESP32BASE_RS485_DEFAULT_TURNAROUND_DELAY_US
#define ESP32BASE_RS485_DEFAULT_TURNAROUND_DELAY_US 0
#endif

class Esp32BaseRs485Port {
public:
    struct Config {
        int8_t rxPin;
        int8_t txPin;
        int8_t dePin;
        uint32_t baud;
        uint32_t serialConfig;
        uint32_t turnaroundDelayUs;
        bool deActiveHigh;
    };

    explicit Esp32BaseRs485Port(HardwareSerial& serial);

    bool configure(int8_t rxPin,
                   int8_t txPin,
                   int8_t dePin,
                   uint32_t baud,
                   uint32_t serialConfig = SERIAL_8N1,
                   uint32_t turnaroundDelayUs = ESP32BASE_RS485_DEFAULT_TURNAROUND_DELAY_US,
                   bool deActiveHigh = true);
    bool configure(const Config& config);
    bool begin();
    bool isBegun() const;

    size_t writeBytes(const uint8_t* data, size_t len);
    size_t writeBytes(const char* text);
    bool readable();
    int readByte();
    int available();

    void setTurnaroundDelayUs(uint32_t delayUs);
    uint32_t turnaroundDelayUs() const;
    void setDirectionReceive();
    void setDirectionTransmit();

private:
    HardwareSerial* _serial;
    Config _config;
    bool _configured;
    bool _begun;

    void setTransmitEnabled(bool enabled);
    void applyTransmitEnabled(bool enabled);
    void waitTurnaround();
};
