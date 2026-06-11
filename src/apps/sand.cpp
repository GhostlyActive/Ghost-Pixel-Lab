#include "sand.h"
#include "tilt.h"
#include "board/display.h"

#include <esp_heap_caps.h>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace apps {

namespace {

using board::gfx::Surface;

constexpr int GW = board::display::WIDTH  / 2;  // 184
constexpr int GH = board::display::HEIGHT / 2;  // 224

constexpr uint16_t PALETTE[] = {
    0x0000,  // 0 = empty
    0xFE60, 0xFD00, 0xFBE0, 0x07E0, 0x07FF, 0xF81F, 0x051F,
};
constexpr int N_COLORS = sizeof(PALETTE) / sizeof(PALETTE[0]) - 1;

constexpr float TILT_MIN = 0.25f;  // g needed to redirect gravity

constexpr uint16_t COL_DIM = 0x8410;

} // namespace

void Sand::onEnter() {
    if (!g_) g_ = static_cast<uint8_t*>(heap_caps_malloc(GW * GH, MALLOC_CAP_SPIRAM));
    if (g_) memset(g_, 0, GW * GH);
    dirX_ = 0;
    dirY_ = 1;
}

void Sand::onExit() {
    free(g_);
    g_ = nullptr;
}

void Sand::stamp(int cx, int cy) {
    for (int dy = -3; dy <= 3; ++dy) {
        for (int dx = -3; dx <= 3; ++dx) {
            const int x = cx + dx, y = cy + dy;
            if (x < 0 || x >= GW || y < 0 || y >= GH) continue;
            if (dx * dx + dy * dy > 10) continue;
            if ((rnd() & 3) == 0) continue;  // keep it grainy
            g_[y * GW + x] = static_cast<uint8_t>(colorIdx_);
        }
    }
}

// One automaton step, gravity straight down (dy=1) or up (dy=-1): a grain
// moves into the empty cell ahead, otherwise tries one random diagonal.
void Sand::stepVertical(int dy) {
    for (int j = 0; j < GH - 1; ++j) {
        const int y  = (dy > 0) ? GH - 2 - j : j + 1;
        const int ny = y + dy;
        const bool ltr = rnd() & 1;
        for (int i = 0; i < GW; ++i) {
            const int x = ltr ? i : GW - 1 - i;
            const uint8_t c = g_[y * GW + x];
            if (!c) continue;
            if (g_[ny * GW + x] == 0) {
                g_[ny * GW + x] = c;
                g_[y * GW + x]  = 0;
                continue;
            }
            const int side = (rnd() & 1) ? 1 : -1;
            for (int k = 0; k < 2; ++k) {
                const int nx = x + (k == 0 ? side : -side);
                if (nx < 0 || nx >= GW) continue;
                if (g_[ny * GW + nx] == 0) {
                    g_[ny * GW + nx] = c;
                    g_[y * GW + x]   = 0;
                    break;
                }
            }
        }
    }
}

// Same automaton with gravity left/right (x and y swapped).
void Sand::stepHorizontal(int dx) {
    for (int j = 0; j < GW - 1; ++j) {
        const int x  = (dx > 0) ? GW - 2 - j : j + 1;
        const int nx = x + dx;
        const bool ttb = rnd() & 1;
        for (int i = 0; i < GH; ++i) {
            const int y = ttb ? i : GH - 1 - i;
            const uint8_t c = g_[y * GW + x];
            if (!c) continue;
            if (g_[y * GW + nx] == 0) {
                g_[y * GW + nx] = c;
                g_[y * GW + x]  = 0;
                continue;
            }
            const int side = (rnd() & 1) ? 1 : -1;
            for (int k = 0; k < 2; ++k) {
                const int ny = y + (k == 0 ? side : -side);
                if (ny < 0 || ny >= GH) continue;
                if (g_[ny * GW + nx] == 0) {
                    g_[ny * GW + nx] = c;
                    g_[y * GW + x]   = 0;
                    break;
                }
            }
        }
    }
}

void Sand::update(const core::Input& in, float dt) {
    if (!g_) return;

    // Gravity follows the dominant tilt axis; near-flat keeps the last one.
    float gx, gy;
    if (tilt::gravity(gx, gy)) {
        if (fabsf(gx) > fabsf(gy)) {
            if (fabsf(gx) > TILT_MIN) { dirX_ = gx > 0 ? 1 : -1; dirY_ = 0; }
        } else {
            if (fabsf(gy) > TILT_MIN) { dirY_ = gy > 0 ? 1 : -1; dirX_ = 0; }
        }
    }

    // Pour while touching; CLR button top-right clears the box.
    if (in.justReleased && in.startY < 30 &&
        in.startX > board::display::WIDTH - 80 &&
        abs(in.x - in.startX) + abs(in.y - in.startY) < 20) {
        memset(g_, 0, GW * GH);
    } else if (in.pressed) {
        stamp(in.x / 2, in.y / 2);
    }

    colorTimer_ += dt;
    if (colorTimer_ > 0.7f) {
        colorTimer_ = 0;
        colorIdx_ = 1 + (colorIdx_ % N_COLORS);
    }

    if (dirY_ != 0) stepVertical(dirY_);
    else            stepHorizontal(dirX_);
}

void Sand::render(Surface& s) {
    if (!g_) {
        s.clear(0x0000);
        s.text(40, 200, "PSRAM alloc failed", 0xF800, 2);
        return;
    }

    // Grid fills the whole screen at 2x2 px per cell; write spans directly
    // (panel byte order, hence Surface::toPanel on the palette).
    static uint16_t pal[N_COLORS + 1];
    for (int i = 0; i <= N_COLORS; ++i) pal[i] = Surface::toPanel(PALETTE[i]);

    for (int y = 0; y < GH; ++y) {
        const uint8_t* src = &g_[y * GW];
        uint16_t* r0 = &s.pixels[(y * 2) * s.width];
        uint16_t* r1 = r0 + s.width;
        for (int x = 0; x < GW; ++x) {
            const uint16_t c = pal[src[x]];
            r0[0] = c; r0[1] = c;
            r1[0] = c; r1[1] = c;
            r0 += 2; r1 += 2;
        }
    }

    // HUD overlay: current color swatch + clear button.
    s.fillRect(8, 8, 18, 18, PALETTE[colorIdx_]);
    s.text(34, 12, "tilt me", COL_DIM, 1);
    s.fillRect(board::display::WIDTH - 64, 4, 56, 24, 0x2945);
    s.text(board::display::WIDTH - 55, 9, "CLR", 0xFFFF, 2);
}

} // namespace apps
