// SH8601 1.8" AMOLED, 368x448, driven over QSPI.
//
// The framebuffer lives in PSRAM and is exposed via a Surface (see
// board/surface.h) so drawing primitives work on RAM, not on the panel
// directly. present() pushes the whole buffer to the panel in one DMA
// transaction.
#pragma once

#include "surface.h"
#include <cstdint>

namespace board::display {

inline constexpr int WIDTH  = 368;
inline constexpr int HEIGHT = 448;

// Initialise the QSPI bus, the SH8601 panel and the PSRAM framebuffer.
// 80 MHz is the panel's rated maximum and works reliably on this board.
bool begin(uint32_t pclk_hz = 80'000'000);

// Drawing surface backed by the framebuffer. Cheap value type, grab it once
// per frame and use it freely.
gfx::Surface canvas();

// Push the framebuffer to the panel. Blocks until the SPI transaction is queued.
void present();

// SH8601 brightness, 0..255.
void setBrightness(uint8_t value);

} // namespace board::display
