#include "expander.h"
#include "i2c.h"
#include "pins.h"
#include <Arduino.h>

namespace board::expander {

namespace {

constexpr uint8_t REG_INPUT  = 0x00;
constexpr uint8_t REG_OUTPUT = 0x01;
constexpr uint8_t REG_CONFIG = 0x03;   // 1 = input, 0 = output

uint8_t s_output = 0xFF;
uint8_t s_config = 0xFF;

} // namespace

bool begin() {
    s_output = 0xFF;
    s_config = 0x00;
    if (!i2c::writeReg(pins::TCA9554_ADDR, REG_OUTPUT, s_output)) return false;
    if (!i2c::writeReg(pins::TCA9554_ADDR, REG_CONFIG, s_config)) return false;
    return true;
}

void setMode(uint8_t pin, bool output) {
    if (pin > 7) return;
    if (output) s_config &= ~(1 << pin);
    else        s_config |=  (1 << pin);
    i2c::writeReg(pins::TCA9554_ADDR, REG_CONFIG, s_config);
}

void write(uint8_t pin, bool high) {
    if (pin > 7) return;
    if (high) s_output |=  (1 << pin);
    else      s_output &= ~(1 << pin);
    i2c::writeReg(pins::TCA9554_ADDR, REG_OUTPUT, s_output);
}

bool read(uint8_t pin) {
    if (pin > 7) return false;
    uint8_t v;
    if (!i2c::readReg(pins::TCA9554_ADDR, REG_INPUT, v)) return false;
    return (v >> pin) & 1;
}

void resetPulse(uint8_t pin, uint16_t low_ms, uint16_t settle_ms) {
    write(pin, false);
    delay(low_ms);
    write(pin, true);
    delay(settle_ms);
}

} // namespace board::expander
