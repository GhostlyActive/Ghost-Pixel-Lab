// PCF85063 real-time clock on I2C 0x51.
#pragma once

#include <cstdint>

namespace board::rtc {

struct DateTime {
    uint16_t year;    // full year e.g. 2026
    uint8_t  month;   // 1..12
    uint8_t  day;     // 1..31
    uint8_t  hour;    // 0..23
    uint8_t  minute;  // 0..59
    uint8_t  second;  // 0..59
};

bool begin();
bool read(DateTime& dt);
bool write(const DateTime& dt);

} // namespace board::rtc
