// AXP2101 power-management IC on I2C 0x34.
// Reports battery state and exposes the ALDO/BLDO rails used on this board.
#pragma once

#include <cstdint>

namespace board::power {

bool begin();

[[nodiscard]] uint16_t batteryMillivolts();
[[nodiscard]] float    batteryVolts();
[[nodiscard]] int      batteryPercent();   // -1 if read fails
[[nodiscard]] uint16_t vbusMillivolts();
[[nodiscard]] bool     vbusPresent();
[[nodiscard]] bool     charging();

void setALDO(uint8_t n, uint16_t millivolts, bool enable);   // n = 1..4
void setBLDO(uint8_t n, uint16_t millivolts, bool enable);   // n = 1..2

} // namespace board::power
