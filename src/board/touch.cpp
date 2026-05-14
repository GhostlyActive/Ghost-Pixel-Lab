#include "touch.h"
#include "i2c.h"
#include "pins.h"

namespace board::touch {

namespace {

constexpr uint8_t REG_TD_STATUS = 0x02;

} // namespace

bool begin() {
    return i2c::probe(pins::FT3168_ADDR);
}

Point read() {
    Point p{0, 0, false};
    uint8_t b[5];
    if (!i2c::readRegs(pins::FT3168_ADDR, REG_TD_STATUS, b, 5)) return p;
    const uint8_t n = b[0] & 0x0F;
    if (n == 0) return p;
    p.x = ((b[1] & 0x0F) << 8) | b[2];
    p.y = ((b[3] & 0x0F) << 8) | b[4];
    p.pressed = true;
    return p;
}

} // namespace board::touch
