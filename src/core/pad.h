// Xbox controller over BLE (HID). Works with Xbox One S (model 1708, needs
// current controller firmware — update once via the "Xbox Accessories" app)
// and all Xbox Series controllers.
//
// begin() starts a background task that scans, pairs (hold the controller's
// pair button) and reconnects automatically. Apps read state() once per
// frame; the connection survives app switches.
#pragma once

#include <cstdint>

namespace core::pad {

struct State {
    bool connected = false;

    // Sticks -1..1, deadzone applied. Screen frame: +x right, +y down
    // (stick up = -1, like screen coordinates).
    float lx = 0, ly = 0, rx = 0, ry = 0;
    float lt = 0, rt = 0;  // triggers 0..1 (analog)

    bool a = false, b = false, x = false, y = false;
    bool lb = false, rb = false;          // bumpers
    bool ls = false, rs = false;          // stick clicks
    bool up = false, down = false, left = false, right = false;
    bool menu = false, view = false, xbox = false, share = false;

    uint8_t battery = 0;  // percent; 0 until the controller reports it
};

bool begin();                  // start BLE + connection task (idempotent)
[[nodiscard]] bool  connected();
[[nodiscard]] State state();   // thread-safe snapshot, call once per frame

// Fire-and-forget vibration; the controller stops by itself.
// body = main motors, triggers = impulse motors, each 0..100.
void rumble(uint8_t body, uint8_t triggers = 0, uint16_t durationMs = 300);

} // namespace core::pad
