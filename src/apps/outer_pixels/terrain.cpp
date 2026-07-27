#include "terrain.h"
#include "colors.h"

#include <cmath>

namespace apps::outer {

uint32_t hash2d(int x, int y, uint32_t seed) {
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h;
}

namespace {

// Value noise on a lattice that repeats every `period` cells, which is what
// makes the finished tile wrap seamlessly.
float vnoise(float x, float y, int period, uint32_t seed) {
    int x0 = (int)floorf(x), y0 = (int)floorf(y);
    float fx = x - x0, fy = y - y0;
    fx = fx * fx * (3 - 2 * fx); fy = fy * fy * (3 - 2 * fy);
    auto cv = [&](int ix, int iy) {
        return (hash2d(ix & (period - 1), iy & (period - 1), seed) & 0xFFFF) / 65535.0f;
    };
    float a = cv(x0, y0), b = cv(x0 + 1, y0), c = cv(x0, y0 + 1), d = cv(x0 + 1, y0 + 1);
    return a + (b - a) * fx + (c - a) * fy + (a - b - c + d) * fx * fy;
}

// Height bands, each tinted toward the planet's own colour so no two worlds
// look alike even though they share one generator.
uint16_t bandColor(int h, uint16_t base) {
    if (h < 28)  return blend(0x041F, base, 0.30f);   // water
    if (h < 38)  return blend(0xEF5A, base, 0.30f);   // sand
    if (h < 110) return blend(0x2E68, base, 0.45f);   // grass / lowland
    if (h < 185) return blend(0x8410, base, 0.38f);   // rock
    return blend(0xFFFF, base, 0.15f);                // snow caps
}

} // namespace

void Terrain::generate(int planet, uint16_t base) {
    if (!attached() || planet < 0) return;
    const uint32_t seed = (uint32_t)(planet * 2654435761u);

    for (int j = 0; j < SIZE; ++j)
        for (int i = 0; i < SIZE; ++i) {
            const float u = i / (float)SIZE, v = j / (float)SIZE;
            const float cont = vnoise(u * 4, v * 4, 4, seed);
            const float det  = 0.5f  * vnoise(u * 8,  v * 8,  8,  seed)
                             + 0.3f  * vnoise(u * 16, v * 16, 16, seed)
                             + 0.15f * vnoise(u * 32, v * 32, 32, seed)
                             + 0.05f * vnoise(u * 64, v * 64, 64, seed);
            float e = cont * 0.6f + det * 0.4f;
            e = e * e;                                 // dramatic peaks, flat plains
            int h = (int)(e * 255); if (h < 0) h = 0; if (h > 255) h = 255;
            hm_[j * SIZE + i] = (uint8_t)h;
            cm_[j * SIZE + i] = bandColor(h, base);
        }

    held_ = planet;   // last: a reader polling this must never see a half-built tile
}

} // namespace apps::outer
