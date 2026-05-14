// TCA9554 8-bit I/O expander at I2C 0x20.
// On this board: EXIO0 = LCD_RST, EXIO2 = TP_RST (both active-low).
#pragma once

#include <cstdint>

namespace board::expander {

// Configure all pins as outputs at HIGH (resets released).
bool begin();

void setMode(uint8_t pin, bool output);
void write(uint8_t pin, bool high);
bool read(uint8_t pin);

// Active-low reset pulse on an EXIO pin.
void resetPulse(uint8_t pin, uint16_t low_ms = 20, uint16_t settle_ms = 150);

} // namespace board::expander
