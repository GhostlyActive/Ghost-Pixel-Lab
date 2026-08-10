#include "touch.h"
#include "expander.h"
#include "i2c.h"
#include "pins.h"

#include <Arduino.h>

namespace board::touch {

namespace {

constexpr uint8_t REG_TD_STATUS = 0x02;

} // namespace

bool begin() {
    // The touch controller hangs off EXIO2 (active-low reset). Nothing pulses it at
    // power-up — the expander only parks its outputs high — so the controller
    // sometimes never starts answering on I2C and touch silently comes up
    // dead. Reset it properly, then give it a few tries to enumerate.
    expander::resetPulse(pins::EXIO_TP_RST);
    for (int attempt = 0; attempt < 5; ++attempt) {
        if (i2c::probe(pins::TOUCH_ADDR)) return true;
        delay(20);
    }
    return false;
}

Point read() {
    Point p{0, 0, false};
    uint8_t b[5];
    if (!i2c::readRegs(pins::TOUCH_ADDR, REG_TD_STATUS, b, 5)) return p;
    const uint8_t n = b[0] & 0x0F;
    if (n == 0) return p;
    p.x = ((b[1] & 0x0F) << 8) | b[2];
    p.y = ((b[3] & 0x0F) << 8) | b[4];
    p.pressed = true;
    return p;
}

} // namespace board::touch
