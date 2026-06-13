// Central tuning knobs for the platform. Compile-time only — change, build,
// flash. Everything an app should NOT have to hardcode lives here.
#pragma once

#include <cstdint>

namespace config {

// --- Navigation ------------------------------------------------------------
// BOOT button (top side key) returns to the menu. Apps can take the button
// for themselves by overriding App::capturesBackButton().
inline constexpr bool BOOT_BUTTON_HOME = true;

// Swipe down from the top edge returns to the menu. This is the system-wide
// escape hatch — keep it enabled if apps capture the BOOT button.
inline constexpr bool SWIPE_HOME    = true;
inline constexpr int  SWIPE_EDGE_PX = 40;  // swipe must start this close to the top
inline constexpr int  SWIPE_DIST_PX = 90;  // ... and travel this far down

// --- PWR key (lower side key, read from the AXP2101) ------------------------
// Short press cycles display brightness through the steps below. Apps can
// take the key via App::capturesPowerKey(); its events always arrive in
// core::Input either way. Holding the key ~6 s is a hardware power-off.
inline constexpr bool    PWR_KEY_BRIGHTNESS  = true;
inline constexpr uint8_t BRIGHTNESS_STEPS[]  = {255, 140, 50};

// --- Audio -------------------------------------------------------------------
inline constexpr uint8_t DEFAULT_VOLUME = 30;  // master volume at boot, 0..100

// --- Gamepad -------------------------------------------------------------------
inline constexpr float PAD_DEADZONE = 0.10f;  // stick deadzone, 0..1

// --- Diagnostics ---------------------------------------------------------------
inline constexpr uint32_t STATS_LOG_MS = 2000;  // serial fps/frame log; 0 = off

} // namespace config
