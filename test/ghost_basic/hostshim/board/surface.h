// Host-only stand-in for board/surface.h: just enough of the API for
// Screen::render to draw into a plain RGB565 buffer off-device.
#pragma once
#include <cstdint>

namespace board::gfx {
struct Surface {
    uint16_t* pixels;
    int width;
    int height;

    void px(int x, int y, uint16_t c) {
        if ((unsigned)x < (unsigned)width && (unsigned)y < (unsigned)height)
            pixels[y * width + x] = c;
    }
    void clear(uint16_t c) {
        for (int i = 0; i < width * height; ++i) pixels[i] = c;
    }
    void fillRect(int x, int y, int w, int h, uint16_t c) {
        for (int yy = 0; yy < h; ++yy)
            for (int xx = 0; xx < w; ++xx) px(x + xx, y + yy, c);
    }
};
}
