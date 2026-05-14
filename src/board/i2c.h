// Thin I2C register-access helpers built on top of Arduino's Wire.
// All on-board peripherals share Wire(0); call Wire.begin() once after the
// display has been initialised (see board::display::begin notes).
#pragma once

#include <Wire.h>
#include <cstddef>
#include <cstdint>

namespace board::i2c {

inline bool write(uint8_t addr, const uint8_t* data, size_t n) {
    Wire.beginTransmission(addr);
    Wire.write(data, n);
    return Wire.endTransmission() == 0;
}

inline bool writeReg(uint8_t addr, uint8_t reg, uint8_t value) {
    const uint8_t buf[2] = {reg, value};
    return write(addr, buf, 2);
}

inline bool readRegs(uint8_t addr, uint8_t reg, uint8_t* out, size_t n) {
    Wire.beginTransmission(addr);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    const size_t got = Wire.requestFrom((int)addr, (int)n);
    if (got != n) return false;
    for (size_t i = 0; i < n; ++i) out[i] = Wire.read();
    return true;
}

inline bool readReg(uint8_t addr, uint8_t reg, uint8_t& out) {
    return readRegs(addr, reg, &out, 1);
}

// Update bits in a register: reg = (reg & ~mask) | (value & mask).
inline bool updateReg(uint8_t addr, uint8_t reg, uint8_t mask, uint8_t value) {
    uint8_t cur;
    if (!readReg(addr, reg, cur)) return false;
    cur = (cur & ~mask) | (value & mask);
    return writeReg(addr, reg, cur);
}

inline bool probe(uint8_t addr) {
    Wire.beginTransmission(addr);
    return Wire.endTransmission() == 0;
}

} // namespace board::i2c
