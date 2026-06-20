#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

class TwoWire {
public:
    std::map<uint8_t, std::vector<uint8_t>> devices;
    uint8_t txAddress = 0;
    std::vector<uint8_t> tx;
    std::vector<uint8_t> rx;
    size_t rxIndex = 0;
    bool failEnd = false;

    void begin() {}
    void begin(int, int, uint32_t = 100000) {}

    void beginTransmission(uint8_t address) {
        txAddress = address;
        tx.clear();
    }

    size_t write(uint8_t value) {
        tx.push_back(value);
        return 1;
    }

    uint8_t endTransmission() {
        if (failEnd || devices.find(txAddress) == devices.end()) {
            return 2;
        }
        if (tx.size() >= 2) {
            uint8_t reg = tx[0];
            std::vector<uint8_t>& mem = devices[txAddress];
            for (size_t i = 1; i < tx.size() && reg < mem.size(); ++i, ++reg) {
                mem[reg] = tx[i];
            }
        }
        return 0;
    }

    uint8_t requestFrom(uint8_t address, uint8_t count) {
        rx.clear();
        rxIndex = 0;
        auto it = devices.find(address);
        if (it == devices.end() || tx.empty()) {
            return 0;
        }
        uint8_t reg = tx[0];
        for (uint8_t i = 0; i < count && reg < it->second.size(); ++i, ++reg) {
            rx.push_back(it->second[reg]);
        }
        return static_cast<uint8_t>(rx.size());
    }

    int available() {
        return static_cast<int>(rx.size() - rxIndex);
    }

    int read() {
        if (rxIndex >= rx.size()) {
            return -1;
        }
        return rx[rxIndex++];
    }
};

extern TwoWire Wire;
