#include "clouds.h"
#include "colors.h"
#include "terrain.h"
#include "board/display.h"

#include <cmath>

namespace apps::outer {

void drawClouds(board::gfx::Surface& s, uint16_t sky, float elapsed) {
    constexpr int W = board::display::WIDTH;
    constexpr int H = board::display::HEIGHT;

    float a = 1.0f - elapsed / DIVE_TIME; if (a < 0) a = 0; if (a > 1) a = 1;
    const uint16_t t1 = blend(sky, 0xFFFF, 0.55f);
    const uint16_t t2 = blend(sky, 0xFFFF, 0.35f);
    const uint16_t t3 = blend(sky, 0xFFFF, 0.18f);
    const int drift = int((1.0f - a) * (1.0f - a) * 220.0f);   // accelerates outward
    const int rad   = int(14.0f * a) + 1;
    int idx = 0;

    // Many small mottled puffs in three tints read as texture; a few big blobs
    // would read as shapes moving across the screen.
    for (int gy = -16; gy < H + 24; gy += 24)
        for (int gx = -16; gx < W + 24; gx += 24, ++idx) {
            const uint32_t h = hash2d(gx, gy, 5);
            const float dx = gx - W * 0.5f, dy = gy - H * 0.5f;
            const float dl = 1.0f / sqrtf(dx*dx + dy*dy + 1.0f);
            const int cx = gx + int(h & 15) - 8 + int(dx * dl * drift);
            const int cy = gy + int(h >> 4 & 15) - 8 + int(dy * dl * drift);
            const uint16_t c = (idx % 3 == 0) ? t1 : (idx % 3 == 1) ? t2 : t3;
            s.filledCircle(cx, cy, rad + int(h >> 8 & 3), c);
        }
}

} // namespace apps::outer
