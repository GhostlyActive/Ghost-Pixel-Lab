#include "outer_pixels.h"
#include "core/pad.h"
#include "core/app_manager.h"
#include "board/display.h"

#include <Arduino.h>
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

struct Planet {
    float    x, y, z;               // live position (computed from the orbit)
    float    radius, gm;
    uint16_t col, atmo;
    bool     sun;
    const char* name;
    float    orbR, orbW, inc, node, phase;   // circular orbit around the sun
};

// Field order: x,y,z (live, computed each frame) | radius gm | bodyColor
// skyColor | isSun | name | orbit: radius angularSpeed inclination node phase.
//
// To add a world: append a row. Position comes from the orbit, so just pick a
// distinct orbR (and ideally a different inc/phase) so it has its own lane.
// radius = how big it looks; gm = how hard it pulls (roughly scale with size).
Planet PLANETS[] = {
    {0,0,0,  78, 9000, 0xFE60, 0xFCA0, true,  "Sol",     0,   0,      0,    0,    0   },
    {0,0,0,  14, 2200, 0x5BDF, 0x6D7F, false, "Aqua",    190, 0.100f, 0.15f, 0.0f, 0.0f},
    {0,0,0,  32, 6500, 0xFD20, 0xFCC8, false, "Rust",    300, 0.055f, 0.40f, 1.2f, 1.0f}, // gas giant
    {0,0,0,   8, 1100, 0x9FF3, 0xAEF7, false, "Mint",    240, 0.085f, 0.25f, 2.4f, 2.0f}, // moonlet
    {0,0,0,  20, 3600, 0xF81F, 0xFC9F, false, "Magenta", 430, 0.045f, 0.55f, 3.5f, 0.5f},
    {0,0,0,  11, 1600, 0x07E0, 0x8FEA, false, "Vil",     360, 0.070f, 0.30f, 5.0f, 3.0f},
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

// Position on a planet's inclined circular orbit at angle a.
void orbitPoint(const Planet& p, float a, float& X, float& Y, float& Z) {
    const float lx = p.orbR * cosf(a), lz = p.orbR * sinf(a);
    const float y = lz * sinf(p.inc), z = lz * cosf(p.inc), x = lx;
    X = x * cosf(p.node) + z * sinf(p.node);
    Z = -x * sinf(p.node) + z * cosf(p.node);
    Y = y;
}

void planetPos(const Planet& p, float t, float& X, float& Y, float& Z) {
    if (p.sun) { X = Y = Z = 0; return; }
    orbitPoint(p, p.phase + p.orbW * t, X, Y, Z);
}

void planetVel(const Planet& p, float t, float& vX, float& vY, float& vZ) {
    if (p.sun) { vX = vY = vZ = 0; return; }
    const float a = p.phase + p.orbW * t, da = p.orbW;
    const float dlx = -p.orbR * sinf(a) * da, dlz = p.orbR * cosf(a) * da;
    const float vy = dlz * sinf(p.inc), vz = dlz * cosf(p.inc), vx = dlx;
    vX = vx * cosf(p.node) + vz * sinf(p.node);
    vZ = -vx * sinf(p.node) + vz * cosf(p.node);
    vY = vy;
}

void updatePlanets(float t) {
    for (int i = 0; i < N_PLANETS; ++i)
        planetPos(PLANETS[i], t, PLANETS[i].x, PLANETS[i].y, PLANETS[i].z);
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
        uint16_t c = scaleRGB(base, 0.20f + 0.80f * (i / 64.0f));
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

    t_ = 0;
    updatePlanets(t_);
    px_ = 120; py_ = 40; pz_ = -200;
    vx_ = vy_ = vz_ = 0;
    fwd_[0] = 0; fwd_[1] = 0; fwd_[2] = 1;
    right_[0] = 1; right_[1] = 0; right_[2] = 0;
    up_[0] = 0; up_[1] = 1; up_[2] = 0;
    landed_ = -1;
    showOrbits_ = false; prevA_ = prevY_ = false;
    selected_ = 1;   // Aqua
    for (int i = 0; i < 80; ++i) {
        float x = int(rnd() % 2000) - 1000.0f;
        float y = int(rnd() % 2000) - 1000.0f;
        float z = int(rnd() % 2000) - 1000.0f;
        float l = 1.0f / sqrtf(x * x + y * y + z * z + 1e-3f);
        starX_[i] = x * l; starY_[i] = y * l; starZ_[i] = z * l;
    }
}

void Outer_Pixels::onExit() {}

void Outer_Pixels::update(const core::Input& in, float dt) {
    if (dt <= 0) return;
    if (dt > 0.05f) dt = 0.05f;
    t_ += dt;
    updatePlanets(t_);

    float yawIn = 0, pitchIn = 0, rollIn = 0, thrust = 0;
    bool aBtn = false, yBtn = false;

    auto pad = core::pad::state();
    if (pad.connected) {
        yawIn = pad.lx; pitchIn = pad.ly; rollIn = pad.rx;
        thrust = pad.rt - pad.lt;
        aBtn = pad.a; yBtn = pad.y;
    } else if (in.pressed) {
        if (in.x > W - 110 && in.y > H - 110) thrust = 1.0f;
        else { yawIn = (in.x - in.startX) / 120.0f; pitchIn = (in.y - in.startY) / 120.0f; }
        aBtn = (in.x < 110 && in.y > H - 110);
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
            float pvx, pvy, pvz; planetVel(p, t_, pvx, pvy, pvz);
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
        float dx = px_ - p.x, dy = py_ - p.y, dz = pz_ - p.z;
        const float d = sqrtf(dx*dx + dy*dy + dz*dz);
        if (d >= p.radius + 0.5f) continue;
        const float inv = 1.0f / (d + 1e-3f);
        const float nx = dx*inv, ny = dy*inv, nz = dz*inv;
        px_ = p.x + nx * (p.radius + 0.5f);
        py_ = p.y + ny * (p.radius + 0.5f);
        pz_ = p.z + nz * (p.radius + 0.5f);
        float pvx, pvy, pvz; planetVel(p, t_, pvx, pvy, pvz);
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
}

void Outer_Pixels::render(Surface& s) {
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
            if (PLANETS[i].sun) continue;
            const uint16_t oc = blend(PLANETS[i].col, bg, 0.55f);
            float pxs = 0, pys = 0; bool have = false;
            for (int k = 0; k <= 48; ++k) {
                float wx, wy, wz; orbitPoint(PLANETS[i], k * (6.2832f / 48), wx, wy, wz);
                float sxp, syp, dep;
                if (project(wx, wy, wz, sxp, syp, dep)) {
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

    // HUD.
    char hud[48];
    snprintf(hud, sizeof(hud), "OUTER PIXELS   spd %.0f   %.0f fps", speed, core::manager::fps());
    s.text((W - s.textWidth(hud, 2)) / 2, 8, hud, 0xFFFF, 2);

    if (selected_ >= 0) {
        const Planet& t = PLANETS[selected_];
        const float dx = t.x - px_, dy = t.y - py_, dz = t.z - pz_;
        char nav[40];
        snprintf(nav, sizeof(nav), ">%s  %.0f", t.name,
                 sqrtf(dx*dx + dy*dy + dz*dz) - t.radius);
        s.text((W - s.textWidth(nav, 2)) / 2, 30, nav, 0x07E0, 2);
    }

    if (landed_ >= 0) {
        const char* m = core::pad::connected() ? "LANDED  -  A to launch"
                                               : "LANDED  -  tap lower-left to launch";
        s.text((W - s.textWidth(m, 2)) / 2, H - 60, m, 0x07E0, 2);
    } else if (!core::pad::connected()) {
        const char* m = "searching for controller...";
        s.text((W - s.textWidth(m, 1)) / 2, H - 56, m, 0x8410, 1);
    } else if (near >= 0 && nd < 25.0f) {
        const char* m = (speed < LAND_SPEED + 8) ? "approach: slow to land"
                                                 : "too fast - brake (LT)";
        s.text((W - s.textWidth(m, 1)) / 2, H - 56, m,
               speed < LAND_SPEED + 8 ? 0x07E0 : 0xFD20, 1);
    }

    if (core::pad::connected())
        s.text(8, H - 16, "A target  Y orbits  RT/LT thrust  Rstick roll", 0x8410, 1);
    else
        s.text(8, H - 16, "no pad: drag steer  |  corners: thrust / launch", 0x8410, 1);
}

} // namespace apps
