#include "space_view.h"
#include "bodies.h"
#include "colors.h"
#include "clouds.h"
#include "board/display.h"

#include <cmath>
#include <cstdio>

namespace apps::outer {

namespace {

using board::gfx::Surface;

constexpr int   W = board::display::WIDTH;
constexpr int   H = board::display::HEIGHT;
constexpr float FOC  = 290.0f;          // focal length in pixels
constexpr float NEAR = 0.6f;            // near plane

constexpr float ATMO_SCALE     = 1.6f;  // how far a planet's air reaches, in radii
constexpr float SUN_ATMO_SCALE = 1.8f;
constexpr float LAND_SPEED     = 20.0f; // mirrors ship.cpp, for the approach hint

// A lit sphere, shaded per pixel from a brightness lookup table.
//
// Two tricks keep a screen-filling planet affordable: the normal's z component
// is approximated as 1 - d^2 instead of a square root, and once the disc grows
// past a few hundred pixels it is shaded in blocks. A full-screen sphere would
// otherwise want ~165k square roots per frame, which is exactly the freeze this
// avoids. The result is visually near-identical.
void drawSphere(Surface& s, float scx, float scy, float r, uint16_t base,
                float Lx, float Ly, float Lz, float haze, uint16_t bg) {
    uint16_t lut[65];
    for (int i = 0; i <= 64; ++i) {
        const float bf = i / 64.0f;
        uint16_t c = scaleRGB(base, 0.18f + 0.74f * bf);
        if (bf > 0.86f) c = blend(c, 0xFFFF, (bf - 0.86f) / 0.14f * 0.65f);  // specular hotspot
        if (haze > 0.01f) c = blend(c, bg, haze);
        lut[i] = Surface::toPanel(c);
    }

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

void SpaceView::reset() {
    for (int i = 0; i < STARS; ++i) {
        float x = int(rnd() % 2000) - 1000.0f;
        float y = int(rnd() % 2000) - 1000.0f;
        float z = int(rnd() % 2000) - 1000.0f;
        float l = 1.0f / sqrtf(x * x + y * y + z * z + 1e-3f);
        starX_[i] = x * l; starY_[i] = y * l; starZ_[i] = z * l;
    }
}

void SpaceView::render(Surface& s, const Ship& ship,
                       bool showOrbits, bool padConnected, float diveElapsed) {
    const float fx = ship.fwd[0],   fy = ship.fwd[1],   fz = ship.fwd[2];
    const float rx = ship.right[0], ry = ship.right[1], rz = ship.right[2];
    const float ux = ship.up[0],    uy = ship.up[1],    uz = ship.up[2];
    const float px_ = ship.x, py_ = ship.y, pz_ = ship.z;

    // World point -> screen. Returns false behind the near plane, which is what
    // every caller uses to skip drawing entirely.
    auto project = [&](float wx, float wy, float wz,
                       float& sxp, float& syp, float& depth) -> bool {
        const float dx = wx - px_, dy = wy - py_, dz = wz - pz_;
        depth = dx*fx + dy*fy + dz*fz;
        if (depth < NEAR) return false;
        sxp = W * 0.5f + (dx*rx + dy*ry + dz*rz) / depth * FOC;
        syp = H * 0.5f - (dx*ux + dy*uy + dz*uz) / depth * FOC;
        return true;
    };

    const float speed = ship.speed();

    // The body whose atmosphere we are deepest inside decides the sky.
    int near = -1; float nd = 1e9f;
    for (int i = 0; i < COUNT; ++i) {
        if (BODIES[i].comet) continue;            // comets have no atmosphere
        const float dx = BODIES[i].x - px_, dy = BODIES[i].y - py_, dz = BODIES[i].z - pz_;
        const float d = sqrtf(dx*dx + dy*dy + dz*dz) - BODIES[i].radius;
        if (d < nd) { nd = d; near = i; }
    }

    float atmoT = 0; sky_ = 0x0000;
    if (near >= 0) {
        const float scale = BODIES[near].sun ? SUN_ATMO_SCALE : ATMO_SCALE;
        atmoT = 1.0f - nd / (BODIES[near].radius * scale);
        if (atmoT < 0) atmoT = 0; if (atmoT > 1) atmoT = 1;
        sky_ = BODIES[near].atmo;
    }
    // The sun washes the screen more gently than a planet's sky.
    const float washF = (near >= 0 && BODIES[near].sun) ? 0.45f : 0.9f;
    const uint16_t bg = blend(0x0000, sky_, atmoT * washF);
    s.clear(bg);

    // Stars fade out as the sky brightens, and streak along the direction of
    // travel projected onto the screen.
    const float starFade = 1.0f - atmoT;
    if (starFade > 0.02f) {
        const uint16_t scol = blend(bg, 0xC638, starFade);
        const float vsx = ship.vx*rx + ship.vy*ry + ship.vz*rz;
        const float vsy = ship.vx*ux + ship.vy*uy + ship.vz*uz;
        const float vl = sqrtf(vsx*vsx + vsy*vsy) + 1e-3f;
        const float sdx = vsx / vl, sdy = -vsy / vl;
        const float streak = speed * 0.12f > 16.0f ? 16.0f : speed * 0.12f;
        for (int i = 0; i < STARS; ++i) {
            const float d = starX_[i]*fx + starY_[i]*fy + starZ_[i]*fz;
            if (d < 0.2f) continue;
            const int px = int(W*0.5f + (starX_[i]*rx + starY_[i]*ry + starZ_[i]*rz)/d*FOC);
            const int py = int(H*0.5f - (starX_[i]*ux + starY_[i]*uy + starZ_[i]*uz)/d*FOC);
            if (streak > 1.5f) s.line(px, py, int(px - sdx*streak), int(py - sdy*streak), scol);
            else               s.px(px, py, scol);
        }
    }

    if (showOrbits) {
        for (int i = 0; i < COUNT; ++i) {
            const Body& pb = BODIES[i];
            if (pb.sun || pb.comet) continue;
            const float bx = pb.parent >= 0 ? BODIES[pb.parent].x : 0.0f;
            const float by = pb.parent >= 0 ? BODIES[pb.parent].y : 0.0f;
            const float bz = pb.parent >= 0 ? BODIES[pb.parent].z : 0.0f;
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

    // Painter's algorithm: sort by depth, draw far first. Insertion sort — the
    // list is short and nearly sorted between frames.
    int order[COUNT]; float depth[COUNT];
    for (int i = 0; i < COUNT; ++i) {
        order[i] = i;
        const float dx = BODIES[i].x - px_, dy = BODIES[i].y - py_, dz = BODIES[i].z - pz_;
        depth[i] = dx*fx + dy*fy + dz*fz;
    }
    for (int i = 1; i < COUNT; ++i)
        for (int j = i; j > 0 && depth[order[j]] > depth[order[j-1]]; --j) {
            const int t = order[j]; order[j] = order[j-1]; order[j-1] = t;
        }

    for (int oi = 0; oi < COUNT; ++oi) {
        const Body& p = BODIES[order[oi]];
        float scx, scy, dep;
        if (!project(p.x, p.y, p.z, scx, scy, dep)) continue;
        int r = int(p.radius / dep * FOC);
        if (r < 1) { s.px(int(scx), int(scy), p.col); continue; }
        if (r > 1400) r = 1400;

        const float haze = atmoT * (dep > 80 ? 0.7f : dep / 80 * 0.7f);

        if (p.comet) {
            // The tail streams directly away from the sun, which sits at the
            // origin — so the body's own position is the direction.
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
            // A soft halo of concentric rings fading into the sky, so the edge
            // does not read as a hard circle. Skipped up close: the huge
            // off-screen ring outlines are what tank the frame rate.
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
            // Light direction is the sun, rotated into the ship's view frame.
            float lx = BODIES[SUN].x - p.x, ly = BODIES[SUN].y - p.y, lz = BODIES[SUN].z - p.z;
            const float ll = 1.0f / sqrtf(lx*lx + ly*ly + lz*lz + 1e-3f);
            lx *= ll; ly *= ll; lz *= ll;
            const float Lx = lx*rx + ly*ry + lz*rz;
            const float Ly = lx*ux + ly*uy + lz*uz;
            const float Lz = -(lx*fx + ly*fy + lz*fz);
            drawSphere(s, scx, scy, float(r), p.col, Lx, Ly, Lz, haze, bg);
        }
    }

    // The target: corner brackets when it is on screen, an edge blip pointing
    // at it when it is not (including when it is behind us).
    if (ship.target >= 0) {
        const Body& t = BODIES[ship.target];
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

    // Reentry heating: bands top and bottom, with sparks streaming past.
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

    s.hLine(W/2 - 10, H/2, 21, 0x05FF);
    s.fillRect(W/2, H/2 - 10, 1, 21, 0x05FF);

    char spd[20];
    snprintf(spd, sizeof(spd), "spd %.0f", speed);
    s.text((W - s.textWidth(spd, 2)) / 2, 8, spd, 0x8410, 2);

    // One context hint at a time, most urgent first.
    if (ship.landedOn >= 0) {
        const char* m = padConnected ? "LANDED  -  A to launch"
                                     : "LANDED  -  corner or A to launch";
        s.text((W - s.textWidth(m, 2)) / 2, H - 56, m, 0x07E0, 2);
    } else if (!padConnected) {
        const char* m = "no pad - keys: arrows+Q/E fly  W/S thrust  A pick  Y orbits";
        s.text((W - s.textWidth(m, 1)) / 2, H - 52, m, 0x8410, 1);
    } else if (near >= 0 && nd < 25.0f) {
        const char* m = (speed < LAND_SPEED + 8) ? "approach: slow to land"
                                                 : "too fast - brake (LT)";
        s.text((W - s.textWidth(m, 1)) / 2, H - 52, m,
               speed < LAND_SPEED + 8 ? 0x07E0 : 0xFD20, 1);
    }

    if (ship.target >= 0) {
        const Body& t = BODIES[ship.target];
        const float dx = t.x - px_, dy = t.y - py_, dz = t.z - pz_;
        char nav[40];
        snprintf(nav, sizeof(nav), "%s   %.0f", t.name,
                 sqrtf(dx*dx + dy*dy + dz*dz) - t.radius);
        s.text((W - s.textWidth(nav, 2)) / 2, H - 26, nav, 0x07E0, 2);
    }

    if (diveElapsed >= 0) drawClouds(s, sky_, diveElapsed);
}

} // namespace apps::outer
