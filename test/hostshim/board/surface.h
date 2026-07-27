// Host-only stand-in for board/surface.h: the drawing primitives the apps
// actually call, over a plain RGB565 buffer, with no Arduino behind them.
//
// This is what lets the machine-independent halves of the apps — the BASIC
// screen, the Outer Pixels renderers — be built and checked on a PC. The
// implementations are deliberately naive (correctness only, none of the
// 32-bit span tricks the real Surface uses); only the signatures have to stay
// in step with board/surface.h.
//
// One deliberate difference: the device stores big-endian RGB565 because that
// is the panel's wire format, so its primitives byte-swap on write and code
// that pokes pixels[] directly pre-swaps with toPanel(). Here nothing swaps,
// and toPanel() is the identity to match — that keeps both paths in the same
// colour space, so a test can compare any pixel against a plain RGB565 value.
#pragma once

#include <cmath>
#include <cstdint>
#include <cstdlib>

namespace board::gfx {

struct Surface {
    uint16_t* pixels;
    int width;
    int height;

    static constexpr uint16_t toPanel(uint16_t color) { return color; }

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
    void hLine(int x, int y, int w, uint16_t c) { fillRect(x, y, w, 1, c); }
    void vLine(int x, int y, int h, uint16_t c) { fillRect(x, y, 1, h, c); }

    void line(int x0, int y0, int x1, int y1, uint16_t c) {
        const int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
        const int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
        int err = dx + dy;
        for (;;) {
            px(x0, y0, c);
            if (x0 == x1 && y0 == y1) break;
            const int e2 = 2 * err;
            if (e2 >= dy) { err += dy; x0 += sx; }
            if (e2 <= dx) { err += dx; y0 += sy; }
        }
    }

    void filledCircle(int cx, int cy, int r, uint16_t c) {
        for (int dy = -r; dy <= r; ++dy) {
            const int dx = (int)std::sqrt(float(r * r - dy * dy));
            hLine(cx - dx, cy + dy, 2 * dx + 1, c);
        }
    }

    void circle(int cx, int cy, int r, uint16_t c) {
        int x = r, y = 0, err = 1 - r;
        while (x >= y) {
            px(cx + x, cy + y, c); px(cx - x, cy + y, c);
            px(cx + x, cy - y, c); px(cx - x, cy - y, c);
            px(cx + y, cy + x, c); px(cx - y, cy + x, c);
            px(cx + y, cy - x, c); px(cx - y, cy - x, c);
            ++y;
            if (err < 0) { err += 2 * y + 1; }
            else         { --x; err += 2 * (y - x) + 1; }
        }
    }

    // Text is stubbed to one solid bar per character: the host tests check
    // layout and that something was drawn, never glyph shapes.
    void text(int x, int y, const char* s, uint16_t c, int scale = 1) {
        for (int n = 0; s[n]; ++n)
            fillRect(x + n * 6 * scale, y, 5 * scale, 7 * scale, c);
    }
    int textWidth(const char* s, int scale = 1) const {
        int n = 0; while (s[n]) ++n;
        return n * 6 * scale;
    }
};

} // namespace board::gfx
