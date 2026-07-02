#include "../Esp32BaseProfile.h"

#if ESP32BASE_ENABLE_RS485_PORT

#include "Esp32BaseRs485Port.h"

#include <string.h>

namespace {
bool validRs485Config(const Esp32BaseRs485Port::Config& config) {
    return config.rxPin >= 0 &&
           config.txPin >= 0 &&
           config.dePin >= 0 &&
           config.baud > 0;
}
}

Esp32BaseRs485Port::Esp32BaseRs485Port(HardwareSerial& serial)
    : _serial(&serial),
      _config{-1, -1, -1, 9600, SERIAL_8N1, ESP32BASE_RS485_DEFAULT_TURNAROUND_DELAY_US, true},
      _configured(false),
      _begun(false) {
}

bool Esp32BaseRs485Port::configure(int8_t rxPin,
                                   int8_t txPin,
                                   int8_t dePin,
                                   uint32_t baud,
                                   uint32_t serialConfig,
                                   uint32_t turnaroundDelayUs,
                                   bool deActiveHigh) {
    const Config config{rxPin, txPin, dePin, baud, serialConfig, turnaroundDelayUs, deActiveHigh};
    return configure(config);
}

bool Esp32BaseRs485Port::configure(const Config& config) {
    if (!validRs485Config(config)) {
        _configured = false;
        return false;
    }
    _config = config;
    _configured = true;
    _begun = false;
    return true;
}

bool Esp32BaseRs485Port::begin() {
    if (!_serial || !_configured || !validRs485Config(_config)) {
        return false;
    }
    pinMode(_config.dePin, OUTPUT);
    applyTransmitEnabled(false);
    _serial->begin(static_cast<unsigned long>(_config.baud),
                   _config.serialConfig,
                   _config.rxPin,
                   _config.txPin);
    _begun = true;
    return true;
}

bool Esp32BaseRs485Port::isBegun() const {
    return _begun;
}

size_t Esp32BaseRs485Port::writeBytes(const uint8_t* data, size_t len) {
    if (!_begun || !data || len == 0) {
        return 0;
    }
    setDirectionTransmit();
    waitTurnaround();
    const size_t written = _serial->write(data, len);
    _serial->flush();
    waitTurnaround();
    setDirectionReceive();
    return written;
}

size_t Esp32BaseRs485Port::writeBytes(const char* text) {
    if (!text) {
        return 0;
    }
    return writeBytes(reinterpret_cast<const uint8_t*>(text), strlen(text));
}

bool Esp32BaseRs485Port::readable() {
    return available() > 0;
}

int Esp32BaseRs485Port::readByte() {
    if (!_begun) {
        return -1;
    }
    return _serial->read();
}

int Esp32BaseRs485Port::available() {
    if (!_begun) {
        return 0;
    }
    return _serial->available();
}

void Esp32BaseRs485Port::setTurnaroundDelayUs(uint32_t delayUs) {
    _config.turnaroundDelayUs = delayUs;
}

uint32_t Esp32BaseRs485Port::turnaroundDelayUs() const {
    return _config.turnaroundDelayUs;
}

void Esp32BaseRs485Port::setDirectionReceive() {
    setTransmitEnabled(false);
}

void Esp32BaseRs485Port::setDirectionTransmit() {
    setTransmitEnabled(true);
}

void Esp32BaseRs485Port::setTransmitEnabled(bool enabled) {
    if (!_begun) {
        return;
    }
    applyTransmitEnabled(enabled);
}

void Esp32BaseRs485Port::applyTransmitEnabled(bool enabled) {
    if (!_configured || _config.dePin < 0) {
        return;
    }
    const bool pinHigh = _config.deActiveHigh ? enabled : !enabled;
    digitalWrite(_config.dePin, pinHigh ? HIGH : LOW);
}

void Esp32BaseRs485Port::waitTurnaround() {
    if (_config.turnaroundDelayUs > 0) {
        delayMicroseconds(_config.turnaroundDelayUs);
    }
}

#endif
