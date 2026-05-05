#pragma once

#include <Arduino.h>
#include <stdint.h>

class Esp32BaseMdns {
public:
    static bool begin();
    static void stop();
    static bool isRunning();
    static bool addHttpService(uint16_t port = 80);
};
