#include "bodies.h"

#include <cmath>

namespace apps::outer {

// Order matters: a body's parent must appear before it (sun, planets, moons,
// comets). Columns: radius gm | col atmo | sun comet parent | name | orbit:
// radius angularSpeed inclination node phase.
Body BODIES[COUNT] = {
    {0,0,0, 78, 9000, 0xFE60, 0xFCA0, true,  false, -1, "Helion",  0,   0,      0,    0,    0   },
    {0,0,0, 14, 2200, 0x5BDF, 0x6D7F, false, false,  0, "Sylara",  290, 0.085f, 0.15f, 0.0f, 0.0f},
    {0,0,0, 32, 6500, 0xFD20, 0xFCC8, false, false,  0, "Brontes", 460, 0.045f, 0.40f, 1.2f, 1.0f},
    {0,0,0,  8, 1100, 0x9FF3, 0xAEF7, false, false,  0, "Ione",    370, 0.070f, 0.25f, 2.4f, 2.0f},
    {0,0,0, 20, 3600, 0xF81F, 0xFC9F, false, false,  0, "Astrea",  640, 0.035f, 0.55f, 3.5f, 0.5f},
    {0,0,0, 11, 1600, 0x07E0, 0x8FEA, false, false,  0, "Caelum",  540, 0.055f, 0.30f, 5.0f, 3.0f},
    // moons (parent = a planet index above). Brontes(2) has two. Distinct
    // neutral greys so they read as rock/dust rather than coloured worlds.
    {0,0,0,  4,  120, 0xF79E, 0xF79E, false, false,  2, "Cinder",  62, 0.70f, 0.30f, 0.4f, 0.0f},
    {0,0,0,  3,   80, 0x738E, 0x738E, false, false,  2, "Ash",     90, 0.50f, 0.90f, 1.2f, 2.0f},
    {0,0,0,  3,   80, 0xD69A, 0xD69A, false, false,  4, "Dust",    46, 0.85f, 0.50f, 1.2f, 1.0f},
    {0,0,0,  3,   70, 0xB596, 0xB596, false, false,  1, "Mica",    34, 0.95f, 0.25f, 0.0f, 0.5f},
    {0,0,0,  4,  100, 0x9492, 0x9492, false, false,  5, "Flint",   42, 0.80f, 0.60f, 3.0f, 1.5f},
    // comets (parent = sun) — nucleus + tail pointing away from the sun
    {0,0,0,  3,    0, 0xAEFF, 0x0000, false, true,   0, "Wisp",    400, 0.090f, 0.95f, 2.0f, 0.0f},
    {0,0,0,  2,    0, 0xCEFF, 0x0000, false, true,   0, "Shard",   500, 0.075f, 1.30f, 4.0f, 1.5f},
    {0,0,0,  3,    0, 0xBFEF, 0x0000, false, true,   0, "Tine",    340, 0.105f, 1.10f, 3.2f, 2.5f},
    {0,0,0,  2,    0, 0xDFFF, 0x0000, false, true,   0, "Vane",    560, 0.065f, 0.70f, 5.0f, 5.5f},
};
static_assert(sizeof(BODIES) / sizeof(BODIES[0]) == COUNT,
              "COUNT in bodies.h must match the table above");

void orbitPoint(const Body& b, float a, float& X, float& Y, float& Z) {
    const float lx = b.orbR * cosf(a), lz = b.orbR * sinf(a);
    const float y = lz * sinf(b.inc), z = lz * cosf(b.inc), x = lx;
    X = x * cosf(b.node) + z * sinf(b.node);
    Z = -x * sinf(b.node) + z * cosf(b.node);
    Y = y;
}

void bodyVelocity(int idx, float t, float& vX, float& vY, float& vZ) {
    const Body& b = BODIES[idx];
    if (b.parent < 0) { vX = vY = vZ = 0; return; }
    float pvx, pvy, pvz; bodyVelocity(b.parent, t, pvx, pvy, pvz);
    const float a = b.phase + b.orbW * t, da = b.orbW;
    const float dlx = -b.orbR * sinf(a) * da, dlz = b.orbR * cosf(a) * da;
    const float vy = dlz * sinf(b.inc), vz = dlz * cosf(b.inc), vx = dlx;
    vX = pvx + vx * cosf(b.node) + vz * sinf(b.node);
    vZ = pvz + (-vx * sinf(b.node) + vz * cosf(b.node));
    vY = pvy + vy;
}

// Parents come earlier in the table, so one forward pass is enough — by the
// time a moon is reached its planet already holds this frame's position.
void update(float t) {
    for (int i = 0; i < COUNT; ++i) {
        const Body& b = BODIES[i];
        float ox = 0, oy = 0, oz = 0;
        if (b.parent >= 0) orbitPoint(b, b.phase + b.orbW * t, ox, oy, oz);
        const float bx = b.parent >= 0 ? BODIES[b.parent].x : 0.0f;
        const float by = b.parent >= 0 ? BODIES[b.parent].y : 0.0f;
        const float bz = b.parent >= 0 ? BODIES[b.parent].z : 0.0f;
        BODIES[i].x = bx + ox; BODIES[i].y = by + oy; BODIES[i].z = bz + oz;
    }
}

} // namespace apps::outer
