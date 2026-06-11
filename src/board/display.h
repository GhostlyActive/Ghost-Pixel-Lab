// SH8601 1.8" AMOLED, 368x448, driven over QSPI.
//
// Double-buffered async pipeline: two framebuffers live in PSRAM. The app
// draws into one via a Surface (see board/surface.h) while a background
// task streams the other to the panel, so drawing and transfer overlap.
#pragma once

#include "surface.h"
#include <cstdint>

namespace board::display {

inline constexpr int WIDTH  = 368;
inline constexpr int HEIGHT = 448;

// Initialise the QSPI bus, the SH8601 panel, both PSRAM framebuffers and
// the presenter task. 80 MHz is the panel's rated maximum.
bool begin(uint32_t pclk_hz = 80'000'000);

// Drawing surface for the current frame. The buffers alternate, so the
// returned buffer holds the frame from TWO frames ago — redraw everything
// each frame (a clear() first does the job).
gfx::Surface canvas();

// Flip: hand the drawn buffer to the presenter task and return. Blocks only
// until the PREVIOUS frame's transfer has finished, not the current one's.
void present();

// SH8601 brightness, 0..255.
void setBrightness(uint8_t value);

} // namespace board::display
