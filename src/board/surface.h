// Surface: a writable 16-bit RGB565 buffer with a small set of fast
// drawing primitives. Designed for the SH8601 framebuffer in PSRAM.
//
// All functions clip against (0..w, 0..h). 32-bit writes are used for
// horizontal spans and full clears because PSRAM is wide-bus friendly.
//
// Storage is big-endian RGB565 (the panel's wire format): every primitive
// byte-swaps its color argument once on entry, so present() can stream the
// buffer to the panel as-is instead of swapping all 330 KB each frame.
// API colors stay ordinary RGB565 — only poke pixels[] directly if you
// remember the swap.
#pragma once

#include "font5x7.h"
#include <Arduino.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <cmath>

namespace board::gfx {

struct Surface {
    uint16_t* pixels;
    int       width;
    int       height;

    static constexpr uint16_t toPanel(uint16_t color) {
        return __builtin_bswap16(color);
    }

    // RGB565 from 8-bit channels.
    static constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
        return uint16_t(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
    }

    [[nodiscard]] bool inBounds(int x, int y) const {
        return (unsigned)x < (unsigned)width && (unsigned)y < (unsigned)height;
    }

    void px(int x, int y, uint16_t color) {
        if (inBounds(x, y)) pixels[y * width + x] = toPanel(color);
    }

    void clear(uint16_t color) {
        const uint16_t c = toPanel(color);
        if ((c >> 8) == (c & 0xFF)) {  // black, white, ...: memset is fastest
            memset(pixels, c & 0xFF, size_t(width) * height * 2);
            return;
        }
        const uint32_t v = uint32_t(c) | (uint32_t(c) << 16);
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
        const uint16_t c  = toPanel(color);
        const uint32_t cc = uint32_t(c) | (uint32_t(c) << 16);
        for (int yy = 0; yy < h; ++yy) {
            uint16_t* p = &pixels[(y + yy) * width + x];
            int n = w;
            if (reinterpret_cast<uintptr_t>(p) & 3) { *p++ = c; --n; }
            auto* p32 = reinterpret_cast<uint32_t*>(p);
            for (; n >= 2; n -= 2) *p32++ = cc;
            if (n) *reinterpret_cast<uint16_t*>(p32) = c;
        }
    }

    void hLine(int x, int y, int w, uint16_t color) {
        fillRect(x, y, w, 1, color);
    }

    void vLine(int x, int y, int h, uint16_t color) {
        fillRect(x, y, 1, h, color);
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

    // Midpoint circle outline, 1 px wide.
    void circle(int cx, int cy, int r, uint16_t color) {
        int x = r, y = 0, err = 1 - r;
        while (x >= y) {
            px(cx + x, cy + y, color); px(cx - x, cy + y, color);
            px(cx + x, cy - y, color); px(cx - x, cy - y, color);
            px(cx + y, cy + x, color); px(cx - y, cy + x, color);
            px(cx + y, cy - x, color); px(cx - y, cy - x, color);
            ++y;
            if (err < 0) { err += 2 * y + 1; }
            else         { --x; err += 2 * (y - x) + 1; }
        }
    }

    // Copy a w*h RGB565 image (row-major, normal byte order), clipped.
    void blit(int x, int y, const uint16_t* src, int w, int h) {
        int sx0 = 0, sy0 = 0;
        if (x < 0) { sx0 = -x; x = 0; }
        if (y < 0) { sy0 = -y; y = 0; }
        const int cw = std::min(w - sx0, width - x);
        const int ch = std::min(h - sy0, height - y);
        for (int yy = 0; yy < ch; ++yy) {
            const uint16_t* sp = src + size_t(sy0 + yy) * w + sx0;
            uint16_t* dp = &pixels[size_t(y + yy) * width + x];
            for (int xx = 0; xx < cw; ++xx) dp[xx] = toPanel(sp[xx]);
        }
    }

    // Like blit(), but pixels matching `key` are transparent (sprites).
    void blitKeyed(int x, int y, const uint16_t* src, int w, int h, uint16_t key) {
        int sx0 = 0, sy0 = 0;
        if (x < 0) { sx0 = -x; x = 0; }
        if (y < 0) { sy0 = -y; y = 0; }
        const int cw = std::min(w - sx0, width - x);
        const int ch = std::min(h - sy0, height - y);
        for (int yy = 0; yy < ch; ++yy) {
            const uint16_t* sp = src + size_t(sy0 + yy) * w + sx0;
            uint16_t* dp = &pixels[size_t(y + yy) * width + x];
            for (int xx = 0; xx < cw; ++xx) {
                if (sp[xx] != key) dp[xx] = toPanel(sp[xx]);
            }
        }
    }

    // Render one ASCII character (printable range only).
    void glyph(int x, int y, char c, uint16_t color, int scale = 1) {
        if (c < 0x20 || c > 0x7E) return;
        const auto& g = font::glyphs[c - 0x20];

        // Fast path for fully visible glyphs: direct span writes, no
        // per-pixel clipping or call overhead. Text is the bulk of most
        // frames, so this is worth the duplication.
        if (x >= 0 && y >= 0 &&
            x + font::CHAR_W * scale <= width &&
            y + font::CHAR_H * scale <= height) {
            const uint16_t pc = toPanel(color);
            uint16_t* row0 = &pixels[y * width + x];
            for (int row = 0; row < font::CHAR_H; ++row) {
                for (int sy = 0; sy < scale; ++sy) {
                    uint16_t* p = row0;
                    for (int col = 0; col < font::CHAR_W; ++col, p += scale) {
                        if (g[col] & (1u << row)) {
                            for (int sx = 0; sx < scale; ++sx) p[sx] = pc;
                        }
                    }
                    row0 += width;
                }
            }
            return;
        }

        // Clipped path for glyphs touching an edge.
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
        const int step    = font::advance(scale);
        const int glyphPx = font::CHAR_W * scale;
        while (*s) {
            if (x >= width) break;             // rest is off-screen
            if (x + glyphPx > 0) glyph(x, y, *s, color, scale);
            ++s;
            x += step;
        }
    }

    int textWidth(const char* s, int scale = 1) const {
        int n = 0; while (s[n]) ++n;
        return n * font::advance(scale);
    }
};

} // namespace board::gfx
