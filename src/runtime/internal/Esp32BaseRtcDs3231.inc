#include "Esp32BaseRtcBus.h"
#include "Esp32BaseRtcCalendar.h"

namespace {
using esp32base_internal::epochFromUtcFields;
using esp32base_internal::rtcFromBcd;
using esp32base_internal::rtcReadRegs;
using esp32base_internal::rtcToBcd;
using esp32base_internal::rtcWriteRegs;
using esp32base_internal::utcFieldsFromEpoch;

bool ds3231Probe(TwoWire& wire, uint8_t address) {
    uint8_t status = 0;
    return rtcReadRegs(wire, address, 0x0F, &status, 1);
}

bool ds3231ReadEpoch(TwoWire& wire, uint8_t address, uint32_t* epoch, Esp32BaseRtc::Status* status) {
    uint8_t regs[7];
    uint8_t stat = 0;
    if (!rtcReadRegs(wire, address, 0x00, regs, sizeof(regs))) {
        *status = Esp32BaseRtc::STATUS_MISSING;
        return false;
    }
    if (!rtcReadRegs(wire, address, 0x0F, &stat, 1)) {
        *status = Esp32BaseRtc::STATUS_I2C_ERROR;
        return false;
    }
    if (stat & 0x80U) {
        *status = Esp32BaseRtc::STATUS_CLOCK_STOPPED;
        return false;
    }
    uint8_t second, minute, hour = 0, day, month, year2;
    if (!rtcFromBcd(regs[0] & 0x7F, 59, &second) ||
        !rtcFromBcd(regs[1] & 0x7F, 59, &minute) ||
        !rtcFromBcd(regs[4] & 0x3F, 31, &day) ||
        !rtcFromBcd(regs[5] & 0x1F, 12, &month) ||
        !rtcFromBcd(regs[6], 99, &year2)) {
        *status = Esp32BaseRtc::STATUS_TIME_INVALID;
        return false;
    }
    if (regs[2] & 0x40U) {
        uint8_t hour12 = 0;
        if (!rtcFromBcd(regs[2] & 0x1F, 12, &hour12) || hour12 < 1) {
            *status = Esp32BaseRtc::STATUS_TIME_INVALID;
            return false;
        }
        hour = static_cast<uint8_t>((hour12 % 12U) + ((regs[2] & 0x20U) ? 12U : 0U));
    } else if (!rtcFromBcd(regs[2] & 0x3F, 23, &hour)) {
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

bool ds3231WriteEpoch(TwoWire& wire, uint8_t address, uint32_t epoch, Esp32BaseRtc::Status* status) {
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
        1,
        rtcToBcd(day),
        rtcToBcd(month),
        rtcToBcd(static_cast<uint8_t>(year - 2000))
    };
    if (!rtcWriteRegs(wire, address, 0x00, regs, sizeof(regs))) {
        *status = Esp32BaseRtc::STATUS_I2C_ERROR;
        return false;
    }
    uint8_t stat = 0;
    if (rtcReadRegs(wire, address, 0x0F, &stat, 1)) {
        stat &= static_cast<uint8_t>(~0x80U);
        rtcWriteRegs(wire, address, 0x0F, &stat, 1);
    }
    *status = Esp32BaseRtc::STATUS_OK;
    return true;
}

const Esp32BaseRtcDriverOps kDs3231Ops = {
    "ds3231",
    0x68,
    ds3231Probe,
    ds3231ReadEpoch,
    ds3231WriteEpoch
};
}
