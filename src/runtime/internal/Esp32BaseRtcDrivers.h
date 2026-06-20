#pragma once

#include "../Esp32BaseRtc.h"

struct Esp32BaseRtcDriverOps {
    const char* name;
    uint8_t defaultAddress;
    bool (*probe)(TwoWire& wire, uint8_t address);
    bool (*readEpoch)(TwoWire& wire, uint8_t address, uint32_t* epoch, Esp32BaseRtc::Status* status);
    bool (*writeEpoch)(TwoWire& wire, uint8_t address, uint32_t epoch, Esp32BaseRtc::Status* status);
};
