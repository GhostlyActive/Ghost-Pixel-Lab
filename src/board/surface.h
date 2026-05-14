// Surface: a writable 16-bit RGB565 buffer with a small set of fast
// drawing primitives. Designed for the SH8601 framebuffer in PSRAM.
//
// All functions clip against (0..w, 0..h). 32-bit writes are used for
// horizontal spans and full clears because PSRAM is wide-bus friendly.
#pragma once

#include "font5x7.h"
#include <Arduino.h>
#include <cstdint>
#include <cstdlib>
#include <cmath>

namespace board::gfx {

struct Surface {
    uint16_t* pixels;
    int       width;
    int       height;

    [[nodiscard]] bool inBounds(int x, int y) const {
        return (unsigned)x < (unsigned)width && (unsigned)y < (unsigned)height;
    }

    void px(int x, int y, uint16_t color) {
        if (inBounds(x, y)) pixels[y * width + x] = color;
    }

    void clear(uint16_t color) {
        const uint32_t v = uint32_t(color) | (uint32_t(color) << 16);
        auto* p32 = reinterpret_cast<uint32_t*>(pixels);
        const int n = (width * height) / 2;
        for (int i = 0; i < n; ++i) p32[i] = v;
    }

    void fillRect(int x, int y, int w, int h, uint16_t color) {
        if (x < 0) { w += x; x = 0; }
        if (y < 0) { h += y; y = 0; }
        if (x + w > width)  w = width  - x;
        if (y + h > height) h = height - y;
        if (w <= 0 || h <= 0) return;
        for (int yy = 0; yy < h; ++yy) {
            uint16_t* row = &pixels[(y + yy) * width + x];
            for (int xx = 0; xx < w; ++xx) row[xx] = color;
        }
    }

    void hLine(int x, int y, int w, uint16_t color) {
        fillRect(x, y, w, 1, color);
    }

    // Bresenham line.
    void line(int x0, int y0, int x1, int y1, uint16_t color) {
        const int dx =  std::abs(x1 - x0);
        const int sx = x0 < x1 ? 1 : -1;
        const int dy = -std::abs(y1 - y0);
        const int sy = y0 < y1 ? 1 : -1;
        int err = dx + dy;
        for (;;) {
            px(x0, y0, color);
            if (x0 == x1 && y0 == y1) break;
            const int e2 = 2 * err;
            if (e2 >= dy) { err += dy; x0 += sx; }
            if (e2 <= dx) { err += dx; y0 += sy; }
        }
    }

    // Two-pixel-wide line for cube edges.
    void thickLine(int x0, int y0, int x1, int y1, uint16_t color) {
        line(x0, y0, x1, y1, color);
        if (std::abs(x1 - x0) > std::abs(y1 - y0)) {
            line(x0, y0 + 1, x1, y1 + 1, color);
        } else {
            line(x0 + 1, y0, x1 + 1, y1, color);
        }
    }

    void filledCircle(int cx, int cy, int r, uint16_t color) {
        for (int dy = -r; dy <= r; ++dy) {
            const int dx = (int)std::sqrt(float(r * r - dy * dy));
            hLine(cx - dx, cy + dy, 2 * dx + 1, color);
        }
    }

    // Render one ASCII character (printable range only).
    void glyph(int x, int y, char c, uint16_t color, int scale = 1) {
        if (c < 0x20 || c > 0x7E) return;
        const auto& g = font::glyphs[c - 0x20];
        for (int col = 0; col < font::CHAR_W; ++col) {
            const uint8_t bits = g[col];
            for (int row = 0; row < font::CHAR_H; ++row) {
                if (bits & (1u << row)) {
                    if (scale == 1) {
                        px(x + col, y + row, color);
                    } else {
                        fillRect(x + col * scale, y + row * scale, scale, scale, color);
                    }
                }
            }
        }
    }

    void text(int x, int y, const char* s, uint16_t color, int scale = 1) {
        const int step = font::advance(scale);
        while (*s) {
            glyph(x, y, *s++, color, scale);
            x += step;
        }
    }

    int textWidth(const char* s, int scale = 1) const {
        int n = 0; while (s[n]) ++n;
        return n * font::advance(scale);
    }
};

} // namespace board::gfx
