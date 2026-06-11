// AXP2101 power-management IC on I2C 0x34.
// Reports battery state, the PWR side key and the ALDO/BLDO rails.
#pragma once

#include <cstdint>

namespace board::power {

bool begin();

// PWR-key presses since the last call (events latch in the PMU's IRQ
// registers, so polling at a few Hz loses nothing). Long press fires after
// ~1.5 s while still held; ~6 s is a hardware power-off regardless.
struct KeyEvents {
    bool shortPress;
    bool longPress;
};
KeyEvents readKeyEvents();

[[nodiscard]] uint16_t batteryMillivolts();
[[nodiscard]] float    batteryVolts();
[[nodiscard]] int      batteryPercent();   // -1 if read fails
[[nodiscard]] uint16_t vbusMillivolts();
[[nodiscard]] bool     vbusPresent();
[[nodiscard]] bool     charging();

void setALDO(uint8_t n, uint16_t millivolts, bool enable);   // n = 1..4
void setBLDO(uint8_t n, uint16_t millivolts, bool enable);   // n = 1..2

} // namespace board::power
