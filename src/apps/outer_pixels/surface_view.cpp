#include "surface_view.h"
#include "colors.h"
#include "board/display.h"

#include <cmath>
#include <cstdio>

namespace apps::outer {

namespace {

using board::gfx::Surface;

constexpr int W = board::display::WIDTH;
constexpr int H = board::display::HEIGHT;

constexpr float EXIT_ALT = 160.0f;      // climb above this and we are back in space
constexpr float YAW_RATE = 1.2f;
constexpr float CLIMB    = 70.0f;
constexpr float BASE_SPD = 30.0f, BOOST_SPD = 45.0f;

constexpr float V_FOV = 0.9f, V_ZFAR = 340.0f, V_SCALEY = 180.0f;
constexpr float V_TILT = 40.0f;         // camera look-down (more = look further down)

} // namespace

void SurfaceView::enter() {
    x_ = 128; y_ = 128; alt_ = 75; yaw_ = 0; pitch_ = 0;
}

bool SurfaceView::update(const Controls& c, float dt, const Terrain& terrain) {
    yaw_  += c.yaw * YAW_RATE * dt;
    pitch_ = c.pitch > 1 ? 1 : (c.pitch < -1 ? -1 : c.pitch);
    float spd = BASE_SPD + c.thrust * BOOST_SPD; if (spd < 6) spd = 6;

    x_   += sinf(yaw_) * spd * dt;
    y_   += cosf(yaw_) * spd * dt;
    alt_ += pitch_ * CLIMB * dt;

    // Skim the ridges rather than sinking through them.
    const float ground = terrain.heightAt((int)floorf(x_), (int)floorf(y_));
    if (alt_ < ground + 4.0f) alt_ = ground + 4.0f;

    return !(alt_ > EXIT_ALT || c.leave);
}

void SurfaceView::render(Surface& s, const Terrain& terrain,
                         const Body& planet) const {
    const uint16_t sky = planet.atmo;
    s.clear(sky);

    // The horizon-so-far per screen column: a slice may only paint above it.
    // Marching front to back means the first thing written to a column is the
    // nearest, so no depth comparison is ever needed.
    static int ybuf[W];
    for (int x = 0; x < W; ++x) ybuf[x] = H;

    const int   cstep   = alt_ < 70.0f ? 3 : 2;      // coarser columns up close
    const float horizon = H * 0.5f - V_TILT + pitch_ * 160.0f;
    const float dirx = sinf(yaw_), diry = cosf(yaw_);
    const float rdx  = cosf(yaw_), rdy  = -sinf(yaw_);   // right, on the map

    // Slice spacing grows with distance: detail where it shows, speed where it
    // does not.
    float z = 1.0f, dz = 1.0f;
    while (z < V_ZFAR) {
        const float half  = z * V_FOV;
        const float stepx = (2 * rdx * half) / W, stepy = (2 * rdy * half) / W;
        const float invz  = 1.0f / z;
        float fog = z / V_ZFAR; if (fog > 1) fog = 1;
        float px = x_ + dirx * z - rdx * half;
        float py = y_ + diry * z - rdy * half;

        for (int x = 0; x < W; x += cstep) {
            const int ix = (int)floorf(px), iy = (int)floorf(py);
            const float h = terrain.heightAt(ix, iy);
            const int screenY = (int)((alt_ - h) * invz * V_SCALEY + horizon);
            if (screenY < ybuf[x]) {
                const uint16_t col =
                    Surface::toPanel(blend(terrain.colorAt(ix, iy), sky, fog * 0.85f));
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
    snprintf(hud, sizeof(hud), "SURFACE %s  alt %.0f", planet.name, alt_);
    s.text((W - s.textWidth(hud, 2)) / 2, 8, hud, 0xFFFF, 2);
}

} // namespace apps::outer
