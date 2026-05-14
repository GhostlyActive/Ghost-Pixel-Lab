#include "rtc.h"
#include "i2c.h"
#include "pins.h"

namespace board::rtc {

namespace {

constexpr uint8_t REG_CTRL1   = 0x00;
constexpr uint8_t REG_SECONDS = 0x04;

inline uint8_t bcd2dec(uint8_t v) { return ((v >> 4) * 10) + (v & 0x0F); }
inline uint8_t dec2bcd(uint8_t v) { return ((v / 10) << 4) | (v % 10); }

} // namespace

bool begin() {
    // Clear the stop bit so the clock runs.
    return i2c::writeReg(pins::PCF85063_ADDR, REG_CTRL1, 0x00);
}

bool read(DateTime& dt) {
    uint8_t b[7];
    if (!i2c::readRegs(pins::PCF85063_ADDR, REG_SECONDS, b, 7)) return false;
    dt.second = bcd2dec(b[0] & 0x7F);
    dt.minute = bcd2dec(b[1] & 0x7F);
    dt.hour   = bcd2dec(b[2] & 0x3F);
    dt.day    = bcd2dec(b[3] & 0x3F);
    // b[4] = weekday (ignored)
    dt.month  = bcd2dec(b[5] & 0x1F);
    dt.year   = 2000 + bcd2dec(b[6]);
    return true;
}

bool write(const DateTime& dt) {
    const uint8_t buf[8] = {
        REG_SECONDS,
        dec2bcd(dt.second),
        dec2bcd(dt.minute),
        dec2bcd(dt.hour),
        dec2bcd(dt.day),
        0,
        dec2bcd(dt.month),
        dec2bcd(static_cast<uint8_t>(dt.year % 100)),
    };
    return i2c::write(pins::PCF85063_ADDR, buf, sizeof(buf));
}

} // namespace board::rtc
