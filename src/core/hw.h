// Which on-board peripherals answered during boot. Set once in main.cpp,
// read-only everywhere else; apps use these flags to degrade gracefully.
#pragma once

namespace core::hw {

extern bool imu;    // QMI8658
extern bool touch;  // FT3168
extern bool power;  // AXP2101
extern bool rtc;    // PCF85063

} // namespace core::hw
