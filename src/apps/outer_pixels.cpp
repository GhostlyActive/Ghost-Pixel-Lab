#include "outer_pixels.h"
#include "core/pad.h"
#include "core/keyboard.h"
#include "core/app_manager.h"
#include "board/display.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cmath>
#include <cstdio>

namespace apps {

namespace {

using board::gfx::Surface;

constexpr int   W = board::display::WIDTH;
constexpr int   H = board::display::HEIGHT;
constexpr float FOC  = 290.0f;
constexpr float NEAR = 0.6f;

constexpr float THRUST    = 70.0f;
constexpr float YAWRATE   = 1.6f;
constexpr float PITCHRATE = 1.4f;
constexpr float ROLLRATE  = 1.8f;
constexpr float DRAG      = 0.15f;
constexpr float GRAV      = 1.0f;
constexpr float LAND_SPEED = 20.0f;     // relative to the planet
constexpr float LAUNCH_V   = 26.0f;

// A body: sun, planet, moon or comet. "parent" generalises the orbit so the
// hierarchy stays organised — a moon orbits a planet, a planet/comet orbits the
// sun. Position is recomputed every frame from the parent + the orbit.
struct Planet {
    float    x, y, z;               // live position (computed)
    float    radius, gm;
    uint16_t col, atmo;
    bool     sun, comet;
    int8_t   parent;                // body it orbits (-1 = fixed at the origin)
    const char* name;
    float    orbR, orbW, inc, node, phase;
};

// Order matters: a body's parent must appear before it (sun, planets, moons,
// comets). Columns: radius gm | col atmo | sun comet parent | name | orbit:
// radius angularSpeed inclination node phase.
Planet PLANETS[] = {
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
constexpr int N_PLANETS = sizeof(PLANETS) / sizeof(PLANETS[0]);
constexpr float ATMO_SCALE = 1.6f;
constexpr float SUN_ATMO_SCALE = 1.8f;   // sun's atmosphere reaches less far

// Body-frame rotation helpers (orientation as 3 orthonormal axis vectors).
inline void rotPair(float* a, float* b, float ang) {
    const float c = cosf(ang), s = sinf(ang);
    for (int i = 0; i < 3; ++i) {
        const float na = a[i] * c + b[i] * s;
        b[i] = -a[i] * s + b[i] * c;
        a[i] = na;
    }
}
inline void cross3(const float* a, const float* b, float* o) {
    o[0] = a[1]*b[2] - a[2]*b[1];
    o[1] = a[2]*b[0] - a[0]*b[2];
    o[2] = a[0]*b[1] - a[1]*b[0];
}
inline void norm3(float* a) {
    const float l = 1.0f / sqrtf(a[0]*a[0] + a[1]*a[1] + a[2]*a[2] + 1e-9f);
    a[0] *= l; a[1] *= l; a[2] *= l;
}

inline uint16_t scaleRGB(uint16_t c, float f) {
    if (f < 0) f = 0; if (f > 1) f = 1;
    int r = int(((c >> 11) & 0x1F) * f);
    int g = int(((c >> 5)  & 0x3F) * f);
    int b = int((c & 0x1F) * f);
    return uint16_t((r << 11) | (g << 5) | b);
}

inline uint16_t blend(uint16_t a, uint16_t b, float f) {
    if (f < 0) f = 0; if (f > 1) f = 1;
    const int ar = (a >> 11) & 0x1F, ag = (a >> 5) & 0x3F, ab = a & 0x1F;
    const int br = (b >> 11) & 0x1F, bg = (b >> 5) & 0x3F, bb = b & 0x1F;
    return uint16_t(((ar + int((br-ar)*f)) << 11) | ((ag + int((bg-ag)*f)) << 5) | (ab + int((bb-ab)*f)));
}

// ===== SURFACE (VOXEL HEIGHTMAP) TERRAIN ================================
// Comanche-style "fake 3D" renderer for the low-altitude planet-surface mode:
// a procedural heightmap tile + a per-column ray march. Kept inside the app
// (no separate engine file), grouped in this one clearly marked section.

constexpr int   TILE   = 256, TMASK = 255;  // procedural tile, wraps endlessly
constexpr float HSCALE = 0.36f;             // stored 0..255 -> world height units

constexpr float SURFACE_ENTER = 16.0f;      // space altitude at which we switch in
constexpr float SURFACE_PREP  = 90.0f;      // start building the terrain (background) here
constexpr float SEXIT  = 160.0f;            // climb above this -> back to space
constexpr float SYAW   = 1.2f;              // surface turn rate
constexpr float SCLIMB = 70.0f;             // climb / descend rate
constexpr float SBASE  = 30.0f, SBOOST = 45.0f;       // forward speed (base + throttle)
constexpr float V_FOV  = 0.9f, V_ZFAR = 340.0f, V_SCALEY = 180.0f;
constexpr float V_TILT = 40.0f;             // camera look-down (more = look further down)
constexpr float DIVE_TIME = 0.7f;           // cloud-dive transition length (s)

inline uint32_t thash(int x, int y, uint32_t seed) {
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h;
}
// Tileable value noise (period = power of two in lattice cells).
inline float vnoise(float x, float y, int period, uint32_t seed) {
    int x0 = (int)floorf(x), y0 = (int)floorf(y);
    float fx = x - x0, fy = y - y0;
    fx = fx * fx * (3 - 2 * fx); fy = fy * fy * (3 - 2 * fy);
    auto cv = [&](int ix, int iy) {
        return (thash(ix & (period - 1), iy & (period - 1), seed) & 0xFFFF) / 65535.0f;
    };
    float a = cv(x0, y0), b = cv(x0 + 1, y0), c = cv(x0, y0 + 1), d = cv(x0 + 1, y0 + 1);
    return a + (b - a) * fx + (c - a) * fy + (a - b - c + d) * fx * fy;
}
// Every band is tinted toward the planet's own colour, so each world's terrain
// has a clearly distinct hue instead of all looking the same.
inline uint16_t terrainColor(int h, uint16_t base) {
    if (h < 28)  return blend(0x041F, base, 0.30f);   // water
    if (h < 38)  return blend(0xEF5A, base, 0.30f);   // sand
    if (h < 110) return blend(0x2E68, base, 0.45f);   // grass / lowland
    if (h < 185) return blend(0x8410, base, 0.38f);   // rock
    return blend(0xFFFF, base, 0.15f);                // snow caps
}
// Fill a heightmap + colormap tile: low-freq ranges + finer octaves. seed
// varies the shape, base tints the grass so each planet looks different.
void genTile(uint8_t* hm, uint16_t* cm, uint32_t seed, uint16_t base) {
    for (int j = 0; j < TILE; ++j)
        for (int i = 0; i < TILE; ++i) {
            const float u = i / (float)TILE, v = j / (float)TILE;
            const float cont = vnoise(u * 4, v * 4, 4, seed);
            const float det  = 0.5f  * vnoise(u * 8,  v * 8,  8,  seed)
                             + 0.3f  * vnoise(u * 16, v * 16, 16, seed)
                             + 0.15f * vnoise(u * 32, v * 32, 32, seed)
                             + 0.05f * vnoise(u * 64, v * 64, 64, seed);
            float e = cont * 0.6f + det * 0.4f;
            e = e * e;                                 // dramatic peaks, flat plains
            int h = (int)(e * 255); if (h < 0) h = 0; if (h > 255) h = 255;
            hm[j * TILE + i] = (uint8_t)h;
            cm[j * TILE + i] = terrainColor(h, base);
        }
}

// Position on a planet's inclined circular orbit at angle a.
void orbitPoint(const Planet& p, float a, float& X, float& Y, float& Z) {
    const float lx = p.orbR * cosf(a), lz = p.orbR * sinf(a);
    const float y = lz * sinf(p.inc), z = lz * cosf(p.inc), x = lx;
    X = x * cosf(p.node) + z * sinf(p.node);
    Z = -x * sinf(p.node) + z * cosf(p.node);
    Y = y;
}

// World-space velocity of a body = parent velocity + own orbital velocity.
void bodyVel(int idx, float t, float& vX, float& vY, float& vZ) {
    const Planet& p = PLANETS[idx];
    if (p.parent < 0) { vX = vY = vZ = 0; return; }
    float pvx, pvy, pvz; bodyVel(p.parent, t, pvx, pvy, pvz);
    const float a = p.phase + p.orbW * t, da = p.orbW;
    const float dlx = -p.orbR * sinf(a) * da, dlz = p.orbR * cosf(a) * da;
    const float vy = dlz * sinf(p.inc), vz = dlz * cosf(p.inc), vx = dlx;
    vX = pvx + vx * cosf(p.node) + vz * sinf(p.node);
    vZ = pvz + (-vx * sinf(p.node) + vz * cosf(p.node));
    vY = pvy + vy;
}

// Recompute every body from its orbit around its parent. Parents come earlier
// in the table, so one forward pass is enough.
void updatePlanets(float t) {
    for (int i = 0; i < N_PLANETS; ++i) {
        const Planet& p = PLANETS[i];
        float ox = 0, oy = 0, oz = 0;
        if (p.parent >= 0) orbitPoint(p, p.phase + p.orbW * t, ox, oy, oz);
        const float bx = p.parent >= 0 ? PLANETS[p.parent].x : 0.0f;
        const float by = p.parent >= 0 ? PLANETS[p.parent].y : 0.0f;
        const float bz = p.parent >= 0 ? PLANETS[p.parent].z : 0.0f;
        PLANETS[i].x = bx + ox; PLANETS[i].y = by + oy; PLANETS[i].z = bz + oz;
    }
}

// Smooth lit sphere via per-pixel Lambert shading. (Lx,Ly,Lz) is the light
// direction in view space. Iterates only the on-screen part of the disc, so
// the cost is bounded by the screen, not the (possibly huge) radius.
void drawSphere(Surface& s, float scx, float scy, float r, uint16_t base,
                float Lx, float Ly, float Lz, float haze, uint16_t bg) {
    // Precompute brightness -> panel color once; the per-pixel loop is then
    // just an index + store (no scaleRGB/blend per pixel). Keeps a screen-
    // filling planet fast.
    uint16_t lut[65];
    for (int i = 0; i <= 64; ++i) {
        const float bf = i / 64.0f;
        uint16_t c = scaleRGB(base, 0.18f + 0.74f * bf);
        if (bf > 0.86f) c = blend(c, 0xFFFF, (bf - 0.86f) / 0.14f * 0.65f);  // specular hotspot
        if (haze > 0.01f) c = blend(c, bg, haze);
        lut[i] = Surface::toPanel(c);
    }
    // Shade in blocks once the planet grows, and approximate the surface
    // normal's z (nz ~= 1 - d^2) so the inner loop has NO per-pixel sqrt.
    // A screen-filling sphere would otherwise be ~165k sqrt/frame -> the
    // freeze. This is visually near-identical but many times cheaper.
    const int step = r > 360 ? 3 : (r > 140 ? 2 : 1);
    int y0 = int(scy - r), y1 = int(scy + r);
    if (y0 < 0) y0 = 0; if (y1 > H - 1) y1 = H - 1;
    const float invr = 1.0f / r;
    for (int y = y0; y <= y1; y += step) {
        const float sy = (y - scy) * invr;
        const float sy2 = sy * sy;
        if (sy2 >= 1.0f) continue;
        const float w = sqrtf(1.0f - sy2);     // one sqrt per row, not per pixel
        int x0 = int(scx - w * r), x1 = int(scx + w * r);
        if (x0 < 0) x0 = 0; if (x1 > W - 1) x1 = W - 1;
        const float nsy = -sy;
        for (int x = x0; x <= x1; x += step) {
            const float sx = (x - scx) * invr;
            const float d2 = sx * sx + sy2;
            if (d2 > 1.0f) continue;
            const float nz = 1.0f - d2;        // cheap normal-z approximation
            float b = sx * Lx + nsy * Ly + nz * Lz;
            int bi = int(b * 64.0f);
            if (bi < 0) bi = 0; if (bi > 64) bi = 64;
            const uint16_t c = lut[bi];
            const int xe = x + step > W ? W : x + step;
            const int ye = y + step > H ? H : y + step;
            for (int yy = y; yy < ye; ++yy) {
                uint16_t* pp = &s.pixels[yy * s.width + x];
                for (int xx = x; xx < xe; ++xx) *pp++ = c;
            }
        }
    }
}

} // namespace

void Outer_Pixels::onEnter() {
    core::pad::begin();          // start searching for an Xbox controller (background)
    core::keyboard::beginSerial();  // typed keys as fallback; leaves BLE to the pad

    kyaw_ = kpitch_ = kroll_ = kthrust_ = 0;
    kA_ = kY_ = kB_ = false;

    t_ = 0;
    updatePlanets(t_);
    px_ = 120; py_ = 40; pz_ = -200;
    vx_ = vy_ = vz_ = 0;
    fwd_[0] = 0; fwd_[1] = 0; fwd_[2] = 1;
    right_[0] = 1; right_[1] = 0; right_[2] = 0;
    up_[0] = 0; up_[1] = 1; up_[2] = 0;
    landed_ = -1;
    showOrbits_ = false; prevA_ = prevY_ = false;
    selected_ = -1;   // nothing targeted until you pick one with A
    for (int i = 0; i < 80; ++i) {
        float x = int(rnd() % 2000) - 1000.0f;
        float y = int(rnd() % 2000) - 1000.0f;
        float z = int(rnd() % 2000) - 1000.0f;
        float l = 1.0f / sqrtf(x * x + y * y + z * z + 1e-3f);
        starX_[i] = x * l; starY_[i] = y * l; starZ_[i] = z * l;
    }

    surface_ = -1;
    // Allocate the terrain buffers and start the background generator once.
    if (!hmap_) hmap_ = static_cast<uint8_t*>(heap_caps_malloc(TILE * TILE, MALLOC_CAP_SPIRAM));
    if (!cmap_) cmap_ = static_cast<uint16_t*>(heap_caps_malloc(TILE * TILE * 2, MALLOC_CAP_SPIRAM));
    if (!genTask_ && hmap_ && cmap_)
        xTaskCreatePinnedToCore(&Outer_Pixels::genTaskTramp, "terrain", 4096, this, 2,
                                reinterpret_cast<TaskHandle_t*>(&genTask_), 0);
}

void Outer_Pixels::onExit() {
    surface_ = -1;   // keep the terrain tile + generator alive (cache for next time)
}

// Background task: build the requested planet's terrain into the tile, then
// park. Runs while you fly in space, so entering the surface never hitches.
void Outer_Pixels::genTaskTramp(void* self) {
    static_cast<Outer_Pixels*>(self)->genLoop();
}
void Outer_Pixels::genLoop() {
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        while (reqPlanet_ != donePlanet_) {
            const int p = reqPlanet_;
            if (p < 0 || !hmap_ || !cmap_) break;
            genTile(hmap_, cmap_, (uint32_t)(p * 2654435761u), PLANETS[p].col);
            donePlanet_ = p;
        }
    }
}

void Outer_Pixels::enterSurface(int planet) {
    if (!hmap_ || !cmap_) return;        // no terrain buffer -> stay in space
    surface_ = planet;                   // tile is already built (background) + cached
    sx_ = 128; sy_ = 128; salt_ = 75; syaw_ = 0; spitch_ = 0;   // low start: terrain in view
}

void Outer_Pixels::exitSurface() {
    const Planet& p = PLANETS[surface_];
    px_ = p.x; py_ = p.y + p.radius + 70; pz_ = p.z;   // pop out above the planet
    vx_ = 0; vy_ = 35; vz_ = 0;
    fwd_[0] = 0; fwd_[1] = 1; fwd_[2] = 0;
    right_[0] = -1; right_[1] = 0; right_[2] = 0;
    up_[0] = 0; up_[1] = 0; up_[2] = 1;
    selected_ = surface_;
    surface_ = -1;
    dive_ = 1; diveT_ = 0;               // punch back up through the clouds
}

void Outer_Pixels::updateSurface(const core::Input& in, float dt) {
    float yawIn = 0, climbIn = 0, throttle = 0;
    bool exitBtn = false;
    auto pad = core::pad::state();
    if (pad.connected) {
        yawIn = pad.lx;
        climbIn = pad.ly;                 // stick up = climb, stick down = descend
        throttle = pad.rt - pad.lt;
        exitBtn = pad.b;
    } else {
        if (in.pressed) {
            yawIn = (in.x - in.startX) / 120.0f;
            climbIn = (in.startY - in.y) / 120.0f;   // drag up = climb
        }
        // Typed keys: up-arrow climbs (kpitch_ is negative for nose-up).
        yawIn += kyaw_; climbIn -= kpitch_;
        throttle += kthrust_;
        exitBtn = exitBtn || kB_;
    }

    syaw_  += yawIn * SYAW * dt;
    spitch_ = climbIn > 1 ? 1 : (climbIn < -1 ? -1 : climbIn);   // + = up/climb
    float spd = SBASE + throttle * SBOOST; if (spd < 6) spd = 6;

    sx_  += sinf(syaw_) * spd * dt;
    sy_  += cosf(syaw_) * spd * dt;
    salt_ += spitch_ * SCLIMB * dt;                  // push up = climb, push down = sink

    const int i = ((int)floorf(sx_)) & TMASK, j = ((int)floorf(sy_)) & TMASK;
    const float hcam = hmap_[j * TILE + i] * HSCALE;
    if (salt_ < hcam + 4.0f) salt_ = hcam + 4.0f;    // skim, don't sink through

    if (salt_ > SEXIT || exitBtn) exitSurface();
}

void Outer_Pixels::renderSurface(Surface& s) {
    const Planet& p = PLANETS[surface_];
    const uint16_t sky = p.atmo;
    s.clear(sky);

    static int ybuf[W];
    for (int x = 0; x < W; ++x) ybuf[x] = H;

    const int   cstep   = salt_ < 70.0f ? 3 : 2;             // adaptive detail
    const float horizon = H * 0.5f - V_TILT + spitch_ * 160.0f;   // look down by V_TILT
    const float dirx = sinf(syaw_), diry = cosf(syaw_);
    const float rdx  = cosf(syaw_), rdy  = -sinf(syaw_);     // right on the map

    float z = 1.0f, dz = 1.0f;
    while (z < V_ZFAR) {
        const float half  = z * V_FOV;
        const float stepx = (2 * rdx * half) / W, stepy = (2 * rdy * half) / W;
        const float invz  = 1.0f / z;
        float fog = z / V_ZFAR; if (fog > 1) fog = 1;
        float px = sx_ + dirx * z - rdx * half;
        float py = sy_ + diry * z - rdy * half;
        for (int x = 0; x < W; x += cstep) {
            const int ix = ((int)floorf(px)) & TMASK, iy = ((int)floorf(py)) & TMASK;
            const float h = hmap_[iy * TILE + ix] * HSCALE;
            const int screenY = (int)((salt_ - h) * invz * V_SCALEY + horizon);
            if (screenY < ybuf[x]) {
                const uint16_t col = Surface::toPanel(blend(cmap_[iy * TILE + ix], sky, fog * 0.85f));
                const int top = screenY < 0 ? 0 : screenY;
                for (int xx = x; xx < x + cstep && xx < W; ++xx) {
                    if (screenY < ybuf[xx]) {
                        uint16_t* pp = &s.pixels[top * s.width + xx];
                        for (int yy = top; yy < ybuf[xx]; ++yy) { *pp = col; pp += s.width; }
                        ybuf[xx] = top;
                    }
                }
            }
            px += stepx * cstep; py += stepy * cstep;
        }
        z += dz; dz *= 1.02f;
    }

    char hud[40];
    snprintf(hud, sizeof(hud), "SURFACE %s  alt %.0f", p.name, salt_);
    s.text((W - s.textWidth(hud, 2)) / 2, 8, hud, 0xFFFF, 2);
    s.text(8, H - 16, core::pad::connected() ? "climb to leave   B exit"
                                             : "drag or arrows to fly   B leave",
           0x8410, 1);

    if (dive_) drawClouds(s, sky);
}

// Cloud layer for the space<->surface transition: puffs cover the screen, then
// shrink and scatter outward to reveal the new view ("punch through clouds").
void Outer_Pixels::drawClouds(Surface& s, uint16_t sky) {
    float a = 1.0f - diveT_ / DIVE_TIME; if (a < 0) a = 0; if (a > 1) a = 1;
    const uint16_t t1 = blend(sky, 0xFFFF, 0.55f);
    const uint16_t t2 = blend(sky, 0xFFFF, 0.35f);
    const uint16_t t3 = blend(sky, 0xFFFF, 0.18f);
    const int drift = int((1.0f - a) * (1.0f - a) * 220.0f);   // accelerates outward
    const int rad   = int(14.0f * a) + 1;
    int idx = 0;
    // many small, mottled puffs (3 tints) -> finer texture than big blobs
    for (int gy = -16; gy < H + 24; gy += 24)
        for (int gx = -16; gx < W + 24; gx += 24, ++idx) {
            const uint32_t h = thash(gx, gy, 5);
            const float dx = gx - W * 0.5f, dy = gy - H * 0.5f;
            const float dl = 1.0f / sqrtf(dx*dx + dy*dy + 1.0f);
            const int cx = gx + int(h & 15) - 8 + int(dx * dl * drift);
            const int cy = gy + int(h >> 4 & 15) - 8 + int(dy * dl * drift);
            const uint16_t c = (idx % 3 == 0) ? t1 : (idx % 3 == 1) ? t2 : t3;
            s.filledCircle(cx, cy, rad + int(h >> 8 & 3), c);
        }
}

// Keyboard fallback: with no pad connected, typed keys fly the ship — from the
// serial console or a BLE keyboard, whichever is delivering. A key press is an
// impulse onto a virtual stick that decays over a fraction of a second, so the
// terminal's auto-repeat reads like holding the stick. Impulses are the honest
// model here: a serial console never reports key releases.
void Outer_Pixels::pollKeys(float dt) {
    const float steerDecay  = expf(-6.0f * dt);
    const float thrustDecay = expf(-2.5f * dt);   // slower: one tap = a burst
    kyaw_ *= steerDecay; kpitch_ *= steerDecay; kroll_ *= steerDecay;
    kthrust_ *= thrustDecay;
    kA_ = kY_ = kB_ = false;

    auto bump = [](float& v, float d) {
        v += d;
        if (v > 1) v = 1; else if (v < -1) v = -1;
    };
    uint8_t k;
    while (core::keyboard::next(k)) {
        switch (k) {
        case 0x9D: bump(kyaw_,  -0.55f); break;   // cursor left
        case 0x1D: bump(kyaw_,   0.55f); break;   // cursor right
        case 0x91: bump(kpitch_, -0.55f); break;  // cursor up = nose up / climb
        case 0x11: bump(kpitch_,  0.55f); break;  // cursor down
        case 'Q': case 'q': bump(kroll_, -0.55f); break;
        case 'E': case 'e': bump(kroll_,  0.55f); break;
        case 'W': case 'w': bump(kthrust_,  0.7f); break;
        case 'S': case 's': bump(kthrust_, -0.7f); break;   // brake / reverse
        case 'A': case 'a': case ' ': kA_ = true; break;    // pick / launch
        case 'Y': case 'y': kY_ = true; break;              // orbit lines
        case 'B': case 'b': kB_ = true; break;              // leave the surface
        default: break;
        }
    }
}

void Outer_Pixels::update(const core::Input& in, float dt) {
    if (dt <= 0) return;
    if (dt > 0.05f) dt = 0.05f;
    pollKeys(dt);
    if (dive_) { diveT_ += dt; if (diveT_ >= DIVE_TIME) dive_ = 0; }
    if (surface_ >= 0) { updateSurface(in, dt); return; }
    t_ += dt;
    updatePlanets(t_);

    float yawIn = 0, pitchIn = 0, rollIn = 0, thrust = 0;
    bool aBtn = false, yBtn = false;

    auto pad = core::pad::state();
    if (pad.connected) {
        yawIn = pad.lx; pitchIn = pad.ly; rollIn = pad.rx;
        thrust = pad.rt - pad.lt;
        aBtn = pad.a; yBtn = pad.y;
    } else {
        if (in.pressed) {
            if (in.x > W - 110 && in.y > H - 110) thrust = 1.0f;
            else { yawIn = (in.x - in.startX) / 120.0f; pitchIn = (in.y - in.startY) / 120.0f; }
            aBtn = (in.x < 110 && in.y > H - 110);
        }
        // Typed keys stack on top of touch — see pollKeys() for the mapping.
        yawIn += kyaw_; pitchIn += kpitch_; rollIn += kroll_;
        thrust += kthrust_;
        if (thrust > 1) thrust = 1; else if (thrust < -1) thrust = -1;
        aBtn = aBtn || kA_; yBtn = yBtn || kY_;
    }

    // Body-relative rotations: yaw around up, pitch around right, roll around
    // forward. Steering is always relative to the ship's current attitude
    // (incl. roll) -> real 6DOF. Re-orthonormalize to kill numeric drift.
    rotPair(fwd_, right_, yawIn * YAWRATE * dt);
    rotPair(fwd_, up_,   -pitchIn * PITCHRATE * dt);
    rotPair(right_, up_,  rollIn * ROLLRATE * dt);
    norm3(fwd_);
    cross3(up_, fwd_, right_); norm3(right_);
    cross3(fwd_, right_, up_);
    const float fx = fwd_[0], fy = fwd_[1], fz = fwd_[2];

    if (yBtn && !prevY_) showOrbits_ = !showOrbits_;
    prevY_ = yBtn;

    if (landed_ >= 0) {
        const Planet& p = PLANETS[landed_];
        px_ = p.x + landOX_; py_ = p.y + landOY_; pz_ = p.z + landOZ_;
        if (aBtn && !prevA_) {
            const float nl = 1.0f / sqrtf(landOX_*landOX_ + landOY_*landOY_ + landOZ_*landOZ_ + 1e-3f);
            float pvx, pvy, pvz; bodyVel(landed_, t_, pvx, pvy, pvz);
            vx_ = pvx + landOX_ * nl * LAUNCH_V;
            vy_ = pvy + landOY_ * nl * LAUNCH_V;
            vz_ = pvz + landOZ_ * nl * LAUNCH_V;
            landed_ = -1;
        }
        prevA_ = aBtn;
        return;
    }

    // A picks the planet under the crosshair (ray-sphere; fallback: smallest angle).
    if (aBtn && !prevA_) {
        int best = -1; float bestT = 1e9f;
        for (int i = 0; i < N_PLANETS; ++i) {
            const float ox = px_ - PLANETS[i].x, oy = py_ - PLANETS[i].y, oz = pz_ - PLANETS[i].z;
            const float b = ox*fx + oy*fy + oz*fz;
            const float c = ox*ox + oy*oy + oz*oz - PLANETS[i].radius * PLANETS[i].radius;
            const float disc = b*b - c;
            if (disc < 0) continue;
            const float tHit = -b - sqrtf(disc);
            if (tHit > 0 && tHit < bestT) { bestT = tHit; best = i; }
        }
        if (best < 0) {
            float bestDot = 0.985f;   // ~10 deg cone
            for (int i = 0; i < N_PLANETS; ++i) {
                float dx = PLANETS[i].x - px_, dy = PLANETS[i].y - py_, dz = PLANETS[i].z - pz_;
                const float l = 1.0f / sqrtf(dx*dx + dy*dy + dz*dz + 1e-3f);
                const float d = (dx*l)*fx + (dy*l)*fy + (dz*l)*fz;
                if (d > bestDot) { bestDot = d; best = i; }
            }
        }
        if (best >= 0) selected_ = best;
    }
    prevA_ = aBtn;

    // Gravity + thrust.
    float ax = fx * thrust * THRUST, ay = fy * thrust * THRUST, az = fz * thrust * THRUST;
    for (int i = 0; i < N_PLANETS; ++i) {
        const float dx = PLANETS[i].x - px_, dy = PLANETS[i].y - py_, dz = PLANETS[i].z - pz_;
        const float d2 = dx*dx + dy*dy + dz*dz + 1.0f;
        const float inv = 1.0f / (d2 * sqrtf(d2));
        const float g = GRAV * PLANETS[i].gm * inv;
        ax += g * dx; ay += g * dy; az += g * dz;
    }
    vx_ += ax * dt; vy_ += ay * dt; vz_ += az * dt;
    const float damp = 1.0f - DRAG * dt;
    vx_ *= damp; vy_ *= damp; vz_ *= damp;
    px_ += vx_ * dt; py_ += vy_ * dt; pz_ += vz_ * dt;

    // Surface contact -> land (slow, relative to the planet) or bounce.
    for (int i = 0; i < N_PLANETS; ++i) {
        const Planet& p = PLANETS[i];
        if (p.sun || p.comet) continue;            // no solid surface to land on
        float dx = px_ - p.x, dy = py_ - p.y, dz = pz_ - p.z;
        const float d = sqrtf(dx*dx + dy*dy + dz*dz);
        if (d >= p.radius + 0.5f) continue;
        const float inv = 1.0f / (d + 1e-3f);
        const float nx = dx*inv, ny = dy*inv, nz = dz*inv;
        px_ = p.x + nx * (p.radius + 0.5f);
        py_ = p.y + ny * (p.radius + 0.5f);
        pz_ = p.z + nz * (p.radius + 0.5f);
        float pvx, pvy, pvz; bodyVel(i, t_, pvx, pvy, pvz);
        const float rvx = vx_ - pvx, rvy = vy_ - pvy, rvz = vz_ - pvz;
        if (sqrtf(rvx*rvx + rvy*rvy + rvz*rvz) < LAND_SPEED) {
            landed_ = i;
            landOX_ = px_ - p.x; landOY_ = py_ - p.y; landOZ_ = pz_ - p.z;
            vx_ = vy_ = vz_ = 0;
        } else {
            const float vn = rvx*nx + rvy*ny + rvz*nz;
            vx_ = pvx + (rvx - 1.5f * vn * nx) * 0.5f;
            vy_ = pvy + (rvy - 1.5f * vn * ny) * 0.5f;
            vz_ = pvz + (rvz - 1.5f * vn * nz) * 0.5f;
        }
        break;
    }

    // Approaching a planet: build its terrain in the background during the
    // descent, then switch into surface mode the instant we are low enough and
    // the tile is ready -> no generation hitch. Tiles are cached per planet.
    if (landed_ < 0) {
        int nearI = -1; float nearAlt = 1e9f;
        for (int i = 0; i < N_PLANETS; ++i) {
            if (PLANETS[i].sun || PLANETS[i].comet || PLANETS[i].radius < 8.0f) continue;
            const float dx = PLANETS[i].x - px_, dy = PLANETS[i].y - py_, dz = PLANETS[i].z - pz_;
            const float a = sqrtf(dx*dx + dy*dy + dz*dz) - PLANETS[i].radius;
            if (a < nearAlt) { nearAlt = a; nearI = i; }
        }
        if (nearI >= 0) {
            if (nearAlt < SURFACE_PREP && reqPlanet_ != nearI && donePlanet_ != nearI) {
                reqPlanet_ = nearI;                              // kick off the build
                if (genTask_) xTaskNotifyGive(static_cast<TaskHandle_t>(genTask_));
            }
            if (nearAlt < SURFACE_ENTER && donePlanet_ == nearI) {
                enterSurface(nearI);
                dive_ = 1; diveT_ = 0;                           // dive through the clouds
            }
        }
    }
}

void Outer_Pixels::render(Surface& s) {
    if (surface_ >= 0) { renderSurface(s); return; }

    const float fx = fwd_[0], fy = fwd_[1], fz = fwd_[2];
    const float rx = right_[0], ry = right_[1], rz = right_[2];
    const float ux = up_[0], uy = up_[1], uz = up_[2];

    auto project = [&](float wx, float wy, float wz,
                       float& sxp, float& syp, float& depth) -> bool {
        const float dx = wx - px_, dy = wy - py_, dz = wz - pz_;
        depth = dx*fx + dy*fy + dz*fz;
        if (depth < NEAR) return false;
        sxp = W * 0.5f + (dx*rx + dy*ry + dz*rz) / depth * FOC;
        syp = H * 0.5f - (dx*ux + dy*uy + dz*uz) / depth * FOC;
        return true;
    };

    const float speed = sqrtf(vx_*vx_ + vy_*vy_ + vz_*vz_);

    int near = -1; float nd = 1e9f;
    for (int i = 0; i < N_PLANETS; ++i) {
        if (PLANETS[i].comet) continue;            // comets have no atmosphere
        const float dx = PLANETS[i].x - px_, dy = PLANETS[i].y - py_, dz = PLANETS[i].z - pz_;
        const float d = sqrtf(dx*dx + dy*dy + dz*dz) - PLANETS[i].radius;
        if (d < nd) { nd = d; near = i; }
    }

    float atmoT = 0; uint16_t sky = 0x0000;
    if (near >= 0) {
        const float scale = PLANETS[near].sun ? SUN_ATMO_SCALE : ATMO_SCALE;
        atmoT = 1.0f - nd / (PLANETS[near].radius * scale);
        if (atmoT < 0) atmoT = 0; if (atmoT > 1) atmoT = 1;
        sky = PLANETS[near].atmo;
    }
    // The sun washes the screen more gently than a planet's sky.
    const float washF = (near >= 0 && PLANETS[near].sun) ? 0.45f : 0.9f;
    const uint16_t bg = blend(0x0000, sky, atmoT * washF);
    s.clear(bg);

    // Stars: fade into the sky, streak with speed.
    const float starFade = 1.0f - atmoT;
    if (starFade > 0.02f) {
        const uint16_t scol = blend(bg, 0xC638, starFade);
        const float vsx = vx_*rx + vy_*ry + vz_*rz, vsy = vx_*ux + vy_*uy + vz_*uz;
        const float vl = sqrtf(vsx*vsx + vsy*vsy) + 1e-3f;
        const float sdx = vsx / vl, sdy = -vsy / vl;
        const float streak = speed * 0.12f > 16.0f ? 16.0f : speed * 0.12f;
        for (int i = 0; i < 80; ++i) {
            const float d = starX_[i]*fx + starY_[i]*fy + starZ_[i]*fz;
            if (d < 0.2f) continue;
            const int px = int(W*0.5f + (starX_[i]*rx + starY_[i]*ry + starZ_[i]*rz)/d*FOC);
            const int py = int(H*0.5f - (starX_[i]*ux + starY_[i]*uy + starZ_[i]*uz)/d*FOC);
            if (streak > 1.5f) s.line(px, py, int(px - sdx*streak), int(py - sdy*streak), scol);
            else               s.px(px, py, scol);
        }
    }

    // Orbit lines (toggle with Y).
    if (showOrbits_) {
        for (int i = 0; i < N_PLANETS; ++i) {
            const Planet& pb = PLANETS[i];
            if (pb.sun || pb.comet) continue;
            const float bx = pb.parent >= 0 ? PLANETS[pb.parent].x : 0.0f;
            const float by = pb.parent >= 0 ? PLANETS[pb.parent].y : 0.0f;
            const float bz = pb.parent >= 0 ? PLANETS[pb.parent].z : 0.0f;
            const uint16_t oc = blend(pb.col, bg, 0.55f);
            float pxs = 0, pys = 0; bool have = false;
            for (int k = 0; k <= 48; ++k) {
                float ox, oy, oz; orbitPoint(pb, k * (6.2832f / 48), ox, oy, oz);
                float sxp, syp, dep;
                if (project(bx + ox, by + oy, bz + oz, sxp, syp, dep)) {
                    if (have) s.line(int(pxs), int(pys), int(sxp), int(syp), oc);
                    pxs = sxp; pys = syp; have = true;
                } else have = false;
            }
        }
    }

    // Planets, painter-sorted far -> near.
    int order[N_PLANETS]; float depth[N_PLANETS];
    for (int i = 0; i < N_PLANETS; ++i) {
        order[i] = i;
        const float dx = PLANETS[i].x - px_, dy = PLANETS[i].y - py_, dz = PLANETS[i].z - pz_;
        depth[i] = dx*fx + dy*fy + dz*fz;
    }
    for (int i = 1; i < N_PLANETS; ++i)
        for (int j = i; j > 0 && depth[order[j]] > depth[order[j-1]]; --j) {
            const int t = order[j]; order[j] = order[j-1]; order[j-1] = t;
        }

    for (int oi = 0; oi < N_PLANETS; ++oi) {
        const Planet& p = PLANETS[order[oi]];
        float scx, scy, dep;
        if (!project(p.x, p.y, p.z, scx, scy, dep)) continue;
        int r = int(p.radius / dep * FOC);
        if (r < 1) { s.px(int(scx), int(scy), p.col); continue; }
        if (r > 1400) r = 1400;

        const float haze = atmoT * (dep > 80 ? 0.7f : dep / 80 * 0.7f);

        if (p.comet) {
            // Tail streams directly away from the sun (origin).
            const float aw = 1.0f / sqrtf(p.x*p.x + p.y*p.y + p.z*p.z + 1e-3f);
            const float ex = p.x*aw, ey = p.y*aw, ez = p.z*aw;
            float tx, ty, tdep;
            if (project(p.x + ex*48, p.y + ey*48, p.z + ez*48, tx, ty, tdep)) {
                s.line(int(scx), int(scy), int(tx), int(ty), blend(p.col, bg, 0.6f));
                s.line(int(scx), int(scy), int((scx+tx)*0.5f), int((scy+ty)*0.5f), p.col);
            }
            s.filledCircle(int(scx), int(scy), r < 2 ? 2 : r, 0xFFFF);   // bright nucleus
            continue;
        }
        if (p.sun) {
            // Soft halo straight from the disc color into the sky, so the
            // edge doesn't read as a hard circle. Skip it up close (the huge
            // off-screen ring outlines are what tank the frame rate).
            if (r < 220) {
                int cg = int(r * 0.6f); if (cg > 30) cg = 30; if (cg < 6) cg = 6;
                for (int k = cg; k >= 1; --k)
                    s.circle(int(scx), int(scy), r + k, blend(0xFE60, bg, float(k) / cg));
            }
            s.filledCircle(int(scx), int(scy), r, 0xFE60);
        } else {
            if (r < 220) {
                int g = int(r * 0.30f); if (g > 18) g = 18; if (g < 3) g = 3;
                for (int k = g; k >= 1; --k)
                    s.circle(int(scx), int(scy), r + k, blend(p.atmo, bg, float(k) / g));
            }
            // Smooth per-pixel lit sphere (light = direction to the sun).
            float lx = PLANETS[0].x - p.x, ly = PLANETS[0].y - p.y, lz = PLANETS[0].z - p.z;
            const float ll = 1.0f / sqrtf(lx*lx + ly*ly + lz*lz + 1e-3f);
            lx *= ll; ly *= ll; lz *= ll;
            const float Lx = lx*rx + ly*ry + lz*rz;
            const float Ly = lx*ux + ly*uy + lz*uz;
            const float Lz = -(lx*fx + ly*fy + lz*fz);
            drawSphere(s, scx, scy, float(r), p.col, Lx, Ly, Lz, haze, bg);
        }
    }

    // Selected-target reticle (or an edge marker if off-screen / behind).
    if (selected_ >= 0) {
        const Planet& t = PLANETS[selected_];
        float scx, scy, dep;
        if (project(t.x, t.y, t.z, scx, scy, dep) &&
            scx > -40 && scx < W + 40 && scy > -40 && scy < H + 40) {
            const int r = int(t.radius / dep * FOC) + 9;
            const uint16_t c = 0x07E0;
            s.line(int(scx)-r, int(scy)-r, int(scx)-r+9, int(scy)-r, c);
            s.line(int(scx)-r, int(scy)-r, int(scx)-r, int(scy)-r+9, c);
            s.line(int(scx)+r, int(scy)+r, int(scx)+r-9, int(scy)+r, c);
            s.line(int(scx)+r, int(scy)+r, int(scx)+r, int(scy)+r-9, c);
        } else {
            const float dx = t.x - px_, dy = t.y - py_, dz = t.z - pz_;
            float cx = dx*rx + dy*ry + dz*rz, cy = dx*ux + dy*uy + dz*uz;
            const float dep2 = dx*fx + dy*fy + dz*fz;
            if (dep2 < 0) { cx = -cx; cy = -cy; }   // behind: flip
            const float l = sqrtf(cx*cx + cy*cy) + 1e-3f;
            const int ax = int(W*0.5f + cx/l * 150), ay = int(H*0.5f - cy/l * 150);
            s.filledCircle(ax, ay, 5, 0x07E0);
        }
    }

    // Reentry heating.
    float heat = atmoT - 0.2f;
    if (heat > 0 && speed > 28.0f) {
        heat *= (speed - 28.0f) / 50.0f; if (heat > 1) heat = 1;
        const uint16_t hot = blend(0xFD20, 0xF800, 0.5f);
        const int band = int(46 * heat) + 6;
        for (int k = 0; k < band; ++k) {
            const uint16_t c = blend(bg, hot, 1.0f - float(k) / band);
            s.hLine(0, k, W, c); s.hLine(0, H - 1 - k, W, c);
        }
        for (int i = 0; i < int(heat * 16); ++i) {
            const int x = rnd() % W; const int len = 8 + rnd() % 20;
            const bool top = rnd() & 1;
            const int yy = top ? int(rnd() % band) : H - 1 - int(rnd() % band);
            s.line(x, yy, x - 6, yy + (top ? len : -len), hot);
        }
    }

    // Crosshair.
    s.hLine(W/2 - 10, H/2, 21, 0x05FF);
    s.fillRect(W/2, H/2 - 10, 1, 21, 0x05FF);

    // Speed, top-centre and dimmed.
    char spd[20];
    snprintf(spd, sizeof(spd), "spd %.0f", speed);
    s.text((W - s.textWidth(spd, 2)) / 2, 8, spd, 0x8410, 2);

    // Context hints.
    if (landed_ >= 0) {
        const char* m = core::pad::connected() ? "LANDED  -  A to launch"
                                               : "LANDED  -  corner or A to launch";
        s.text((W - s.textWidth(m, 2)) / 2, H - 56, m, 0x07E0, 2);
    } else if (!core::pad::connected()) {
        const char* m = "no pad - keys: arrows+Q/E fly  W/S thrust  A pick  Y orbits";
        s.text((W - s.textWidth(m, 1)) / 2, H - 52, m, 0x8410, 1);
    } else if (near >= 0 && nd < 25.0f) {
        const char* m = (speed < LAND_SPEED + 8) ? "approach: slow to land"
                                                 : "too fast - brake (LT)";
        s.text((W - s.textWidth(m, 1)) / 2, H - 52, m,
               speed < LAND_SPEED + 8 ? 0x07E0 : 0xFD20, 1);
    }

    // Target name + distance, bottom-centre (only when something is targeted).
    if (selected_ >= 0) {
        const Planet& t = PLANETS[selected_];
        const float dx = t.x - px_, dy = t.y - py_, dz = t.z - pz_;
        char nav[40];
        snprintf(nav, sizeof(nav), "%s   %.0f", t.name,
                 sqrtf(dx*dx + dy*dy + dz*dz) - t.radius);
        s.text((W - s.textWidth(nav, 2)) / 2, H - 26, nav, 0x07E0, 2);
    }

    if (dive_) drawClouds(s, sky);
}

} // namespace apps
