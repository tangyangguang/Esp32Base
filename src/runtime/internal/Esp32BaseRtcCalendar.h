#pragma once

#include <stdint.h>

namespace esp32base_internal {

inline uint8_t rtcToBcd(uint8_t value) {
    return static_cast<uint8_t>(((value / 10U) << 4U) | (value % 10U));
}

inline bool rtcFromBcd(uint8_t bcdValue, uint8_t maxValue, uint8_t* out) {
    const uint8_t hi = (bcdValue >> 4U) & 0x0FU;
    const uint8_t lo = bcdValue & 0x0FU;
    if (hi > 9 || lo > 9) {
        return false;
    }
    const uint8_t value = static_cast<uint8_t>(hi * 10U + lo);
    if (value > maxValue) {
        return false;
    }
    *out = value;
    return true;
}

inline bool rtcLeapYear(uint16_t year) {
    return (year % 4U == 0U && year % 100U != 0U) || (year % 400U == 0U);
}

inline uint8_t rtcDaysInMonth(uint16_t year, uint8_t month) {
    static const uint8_t days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12) {
        return 0;
    }
    if (month == 2 && rtcLeapYear(year)) {
        return 29;
    }
    return days[month - 1];
}

inline bool epochFromUtcFields(uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute, uint8_t second, uint32_t* epoch) {
    if (!epoch || year < 2000 || year > 2099 || hour > 23 || minute > 59 || second > 59) {
        return false;
    }
    const uint8_t maxDay = rtcDaysInMonth(year, month);
    if (day < 1 || day > maxDay) {
        return false;
    }
    uint32_t days = 0;
    for (uint16_t y = 1970; y < year; ++y) {
        days += rtcLeapYear(y) ? 366UL : 365UL;
    }
    for (uint8_t m = 1; m < month; ++m) {
        days += rtcDaysInMonth(year, m);
    }
    days += static_cast<uint32_t>(day - 1);
    *epoch = days * 86400UL + static_cast<uint32_t>(hour) * 3600UL + static_cast<uint32_t>(minute) * 60UL + second;
    return true;
}

inline bool utcFieldsFromEpoch(uint32_t epoch, uint16_t* year, uint8_t* month, uint8_t* day, uint8_t* hour, uint8_t* minute, uint8_t* second) {
    if (!year || !month || !day || !hour || !minute || !second) {
        return false;
    }
    uint32_t days = epoch / 86400UL;
    uint32_t rem = epoch % 86400UL;
    *hour = static_cast<uint8_t>(rem / 3600UL);
    rem %= 3600UL;
    *minute = static_cast<uint8_t>(rem / 60UL);
    *second = static_cast<uint8_t>(rem % 60UL);

    uint16_t y = 1970;
    while (true) {
        const uint16_t yd = rtcLeapYear(y) ? 366U : 365U;
        if (days < yd) {
            break;
        }
        days -= yd;
        ++y;
    }
    uint8_t m = 1;
    while (true) {
        const uint8_t md = rtcDaysInMonth(y, m);
        if (days < md) {
            break;
        }
        days -= md;
        ++m;
    }
    *year = y;
    *month = m;
    *day = static_cast<uint8_t>(days + 1U);
    return y >= 2000 && y <= 2099;
}

}
