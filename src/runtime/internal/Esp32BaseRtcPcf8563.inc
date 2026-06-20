#include "Esp32BaseRtcBus.h"
#include "Esp32BaseRtcCalendar.h"

namespace {
using esp32base_internal::epochFromUtcFields;
using esp32base_internal::rtcFromBcd;
using esp32base_internal::rtcReadRegs;
using esp32base_internal::rtcToBcd;
using esp32base_internal::rtcWriteRegs;
using esp32base_internal::utcFieldsFromEpoch;

bool pcf8563Probe(TwoWire& wire, uint8_t address) {
    uint8_t control = 0;
    return rtcReadRegs(wire, address, 0x00, &control, 1);
}

bool pcf8563ReadEpoch(TwoWire& wire, uint8_t address, uint32_t* epoch, Esp32BaseRtc::Status* status) {
    uint8_t regs[7];
    if (!rtcReadRegs(wire, address, 0x02, regs, sizeof(regs))) {
        *status = Esp32BaseRtc::STATUS_MISSING;
        return false;
    }
    if (regs[0] & 0x80U) {
        *status = Esp32BaseRtc::STATUS_CLOCK_STOPPED;
        return false;
    }
    uint8_t second, minute, hour, day, month, year2;
    if (!rtcFromBcd(regs[0] & 0x7F, 59, &second) ||
        !rtcFromBcd(regs[1] & 0x7F, 59, &minute) ||
        !rtcFromBcd(regs[2] & 0x3F, 23, &hour) ||
        !rtcFromBcd(regs[3] & 0x3F, 31, &day) ||
        !rtcFromBcd(regs[5] & 0x1F, 12, &month) ||
        !rtcFromBcd(regs[6], 99, &year2)) {
        *status = Esp32BaseRtc::STATUS_TIME_INVALID;
        return false;
    }
    const uint16_t year = static_cast<uint16_t>(2000U + year2);
    if (!epochFromUtcFields(year, month, day, hour, minute, second, epoch)) {
        *status = Esp32BaseRtc::STATUS_TIME_INVALID;
        return false;
    }
    *status = Esp32BaseRtc::STATUS_OK;
    return true;
}

bool pcf8563WriteEpoch(TwoWire& wire, uint8_t address, uint32_t epoch, Esp32BaseRtc::Status* status) {
    uint16_t year;
    uint8_t month, day, hour, minute, second;
    if (!utcFieldsFromEpoch(epoch, &year, &month, &day, &hour, &minute, &second)) {
        *status = Esp32BaseRtc::STATUS_TIME_INVALID;
        return false;
    }
    const uint8_t regs[7] = {
        rtcToBcd(second),
        rtcToBcd(minute),
        rtcToBcd(hour),
        rtcToBcd(day),
        1,
        rtcToBcd(month),
        rtcToBcd(static_cast<uint8_t>(year - 2000))
    };
    if (!rtcWriteRegs(wire, address, 0x02, regs, sizeof(regs))) {
        *status = Esp32BaseRtc::STATUS_I2C_ERROR;
        return false;
    }
    *status = Esp32BaseRtc::STATUS_OK;
    return true;
}

const Esp32BaseRtcDriverOps kPcf8563Ops = {
    "pcf8563",
    0x51,
    pcf8563Probe,
    pcf8563ReadEpoch,
    pcf8563WriteEpoch
};
}
