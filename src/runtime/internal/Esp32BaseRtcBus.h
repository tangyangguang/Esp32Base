#pragma once

#include <stdint.h>
#include <Wire.h>

namespace esp32base_internal {

inline bool rtcReadRegs(TwoWire& wire, uint8_t address, uint8_t reg, uint8_t* data, uint8_t len) {
    wire.beginTransmission(address);
    wire.write(reg);
    if (wire.endTransmission() != 0) {
        return false;
    }
    if (wire.requestFrom(address, len) != len) {
        return false;
    }
    for (uint8_t i = 0; i < len; ++i) {
        const int v = wire.read();
        if (v < 0) {
            return false;
        }
        data[i] = static_cast<uint8_t>(v);
    }
    return true;
}

inline bool rtcWriteRegs(TwoWire& wire, uint8_t address, uint8_t reg, const uint8_t* data, uint8_t len) {
    wire.beginTransmission(address);
    wire.write(reg);
    for (uint8_t i = 0; i < len; ++i) {
        wire.write(data[i]);
    }
    return wire.endTransmission() == 0;
}

}
