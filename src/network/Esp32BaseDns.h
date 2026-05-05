#pragma once

#include <Arduino.h>

class Esp32BaseDns {
public:
    static bool begin();
    static void handle();
    static void stop();
    static bool isRunning();
};
