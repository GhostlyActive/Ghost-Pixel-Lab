#include "power.h"
#include "i2c.h"
#include "pins.h"

namespace board::power {

namespace {

constexpr uint8_t REG_STATUS1    = 0x00;
constexpr uint8_t REG_STATUS2    = 0x01;
constexpr uint8_t REG_IRQ_EN2    = 0x41;
constexpr uint8_t REG_IRQ_STAT2  = 0x49;   // write 1 to clear
constexpr uint8_t REG_VBAT_H     = 0x34;
constexpr uint8_t REG_VBUS_H     = 0x38;
constexpr uint8_t REG_BAT_PCT    = 0xA4;
constexpr uint8_t REG_LDO_EN1    = 0x90;
constexpr uint8_t REG_ALDO1_VOLT = 0x92;
constexpr uint8_t REG_BLDO1_VOLT = 0x96;

// IRQ bank 2: PWR-key events.
constexpr uint8_t PKEY_SHORT = 1 << 3;
constexpr uint8_t PKEY_LONG  = 1 << 2;

uint16_t read14(uint8_t hi_reg) {
    uint8_t b[2];
    if (!i2c::readRegs(pins::AXP2101_ADDR, hi_reg, b, 2)) return 0;
    return ((uint16_t)(b[0] & 0x3F) << 8) | b[1];
}

} // namespace

bool begin() {
    if (!i2c::probe(pins::AXP2101_ADDR)) return false;
    // Latch PWR-key short/long presses in the IRQ status register and start
    // from a clean slate.
    i2c::updateReg(pins::AXP2101_ADDR, REG_IRQ_EN2,
                   PKEY_SHORT | PKEY_LONG, PKEY_SHORT | PKEY_LONG);
    i2c::writeReg(pins::AXP2101_ADDR, REG_IRQ_STAT2, PKEY_SHORT | PKEY_LONG);
    return true;
}

KeyEvents readKeyEvents() {
    KeyEvents ev{false, false};
    uint8_t st = 0;
    if (!i2c::readReg(pins::AXP2101_ADDR, REG_IRQ_STAT2, st)) return ev;
    const uint8_t hit = st & (PKEY_SHORT | PKEY_LONG);
    if (hit) i2c::writeReg(pins::AXP2101_ADDR, REG_IRQ_STAT2, hit);
    ev.shortPress = hit & PKEY_SHORT;
    ev.longPress  = hit & PKEY_LONG;
    return ev;
}

uint16_t batteryMillivolts() { return read14(REG_VBAT_H); }
float    batteryVolts()      { return batteryMillivolts() / 1000.0f; }
uint16_t vbusMillivolts()    { return read14(REG_VBUS_H); }

int batteryPercent() {
    uint8_t v;
    if (!i2c::readReg(pins::AXP2101_ADDR, REG_BAT_PCT, v)) return -1;
    return v > 100 ? 100 : v;
}

bool vbusPresent() {
    uint8_t s;
    if (!i2c::readReg(pins::AXP2101_ADDR, REG_STATUS1, s)) return false;
    return s & (1 << 5);
}

bool charging() {
    uint8_t s;
    if (!i2c::readReg(pins::AXP2101_ADDR, REG_STATUS2, s)) return false;
    return ((s >> 5) & 0x03) == 0x01;
}

void setALDO(uint8_t n, uint16_t millivolts, bool enable) {
    if (n < 1 || n > 4) return;
    const uint8_t step = (millivolts < 500) ? 0 : (millivolts - 500) / 100;
    i2c::writeReg(pins::AXP2101_ADDR, REG_ALDO1_VOLT + (n - 1), step);
    i2c::updateReg(pins::AXP2101_ADDR, REG_LDO_EN1,
                   1 << (n - 1), enable ? (1 << (n - 1)) : 0);
}

void setBLDO(uint8_t n, uint16_t millivolts, bool enable) {
    if (n < 1 || n > 2) return;
    const uint8_t step = (millivolts < 500) ? 0 : (millivolts - 500) / 100;
    i2c::writeReg(pins::AXP2101_ADDR, REG_BLDO1_VOLT + (n - 1), step);
    i2c::updateReg(pins::AXP2101_ADDR, REG_LDO_EN1,
                   1 << (4 + n - 1), enable ? (1 << (4 + n - 1)) : 0);
}

} // namespace board::power
