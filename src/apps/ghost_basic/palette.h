// The 16 Commodore 64 colours (VIC-II), as RGB565 for the panel.
//
// Index order matches the C64's own colour codes, so colour RAM values and
// BASIC's colour numbers map straight into this table. The power-on look is
// light-blue text (14) on a blue background (6) inside a light-blue border.
#pragma once

#include <cstdint>

namespace apps::ghost {

// 8-bit-per-channel -> RGB565 (normal byte order; Surface swaps on write).
constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return uint16_t(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

inline constexpr uint16_t PALETTE[16] = {
    rgb565(0x00, 0x00, 0x00),  // 0  black
    rgb565(0xFF, 0xFF, 0xFF),  // 1  white
    rgb565(0x88, 0x00, 0x00),  // 2  red
    rgb565(0xAA, 0xFF, 0xEE),  // 3  cyan
    rgb565(0xCC, 0x44, 0xCC),  // 4  purple
    rgb565(0x00, 0xCC, 0x55),  // 5  green
    rgb565(0x00, 0x00, 0xAA),  // 6  blue
    rgb565(0xEE, 0xEE, 0x77),  // 7  yellow
    rgb565(0xDD, 0x88, 0x55),  // 8  orange
    rgb565(0x66, 0x44, 0x00),  // 9  brown
    rgb565(0xFF, 0x77, 0x77),  // 10 light red
    rgb565(0x33, 0x33, 0x33),  // 11 dark grey
    rgb565(0x77, 0x77, 0x77),  // 12 grey
    rgb565(0xAA, 0xFF, 0x66),  // 13 light green
    rgb565(0x00, 0x88, 0xFF),  // 14 light blue
    rgb565(0xBB, 0xBB, 0xBB),  // 15 light grey
};

// Named indices for the ones the machine uses by default.
enum Color : uint8_t {
    COL_BLACK = 0,  COL_WHITE = 1,   COL_RED = 2,     COL_CYAN = 3,
    COL_PURPLE = 4, COL_GREEN = 5,   COL_BLUE = 6,    COL_YELLOW = 7,
    COL_ORANGE = 8, COL_BROWN = 9,   COL_LTRED = 10,  COL_DKGREY = 11,
    COL_GREY = 12,  COL_LTGREEN = 13, COL_LTBLUE = 14, COL_LTGREY = 15,
};

inline constexpr uint16_t color(uint8_t i) { return PALETTE[i & 0x0F]; }

} // namespace apps::ghost
