// RGB565 arithmetic shared by the terrain generator and both renderers.
//
// Everything in Outer Pixels fades: sky into space, terrain into fog, planets
// into their own atmosphere. Both helpers work on ordinary RGB565 (the byte
// order the Surface API takes), so a caller only thinks about the panel's
// swapped layout when it writes pixels directly.
#pragma once

#include <cstdint>

namespace apps::outer {

// Scale a colour toward black. f is clamped to 0..1.
inline uint16_t scaleRGB(uint16_t c, float f) {
    if (f < 0) f = 0; if (f > 1) f = 1;
    int r = int(((c >> 11) & 0x1F) * f);
    int g = int(((c >> 5)  & 0x3F) * f);
    int b = int((c & 0x1F) * f);
    return uint16_t((r << 11) | (g << 5) | b);
}

// Linear mix: f = 0 gives a, f = 1 gives b.
inline uint16_t blend(uint16_t a, uint16_t b, float f) {
    if (f < 0) f = 0; if (f > 1) f = 1;
    const int ar = (a >> 11) & 0x1F, ag = (a >> 5) & 0x3F, ab = a & 0x1F;
    const int br = (b >> 11) & 0x1F, bg = (b >> 5) & 0x3F, bb = b & 0x1F;
    return uint16_t(((ar + int((br-ar)*f)) << 11) | ((ag + int((bg-ag)*f)) << 5) | (ab + int((bb-ab)*f)));
}

} // namespace apps::outer
