#include "level.h"
#include "tilt.h"
#include "board/display.h"

#include <cmath>
#include <cstdio>

namespace apps {

namespace {

using board::gfx::Surface;

constexpr int   CX        = board::display::WIDTH / 2;
constexpr int   CY        = 235;
constexpr int   RING_R[3] = {44, 88, 132};   // ~2.5 / 5 / 7.5 deg at PX_PER_G
constexpr float PX_PER_G  = 132.0f / 0.131f; // outer ring = sin(7.5 deg)
constexpr int   BUBBLE_R  = 15;
constexpr float LEVEL_DEG = 0.8f;            // "level!" threshold

constexpr uint16_t COL_ACCENT = 0x07FF;
constexpr uint16_t COL_TEXT   = 0xFFFF;
constexpr uint16_t COL_DIM    = 0x8410;
constexpr uint16_t COL_RING   = 0x2945;
constexpr uint16_t COL_OK     = 0x07E0;

} // namespace

void Level::onEnter() {
    fx_ = fy_ = 0;
    fz_ = 1;
}

void Level::update(const core::Input&, float dt) {
    float gx, gy, gz;
    haveImu_ = tilt::read(gx, gy, gz);
    if (!haveImu_) return;

    const float a = fminf(1.0f, dt * 10.0f);   // ~100 ms low-pass
    fx_ += (gx - fx_) * a;
    fy_ += (gy - fy_) * a;
    fz_ += (gz - fz_) * a;
}

void Level::render(Surface& s) {
    const int W = board::display::WIDTH;
    char line[32];

    s.clear(0x0000);
    s.text((W - s.textWidth("LEVEL", 3)) / 2, 14, "LEVEL", COL_ACCENT, 3);

    if (!haveImu_) {
        s.text((W - s.textWidth("IMU missing", 2)) / 2, 200, "IMU missing", 0xF800, 2);
        return;
    }

    const float tiltDeg = atan2f(sqrtf(fx_ * fx_ + fy_ * fy_), fabsf(fz_)) * 57.2958f;
    const bool  level   = tiltDeg < LEVEL_DEG;

    // Rings + crosshair.
    for (int r : RING_R) s.circle(CX, CY, r, COL_RING);
    s.hLine(CX - RING_R[2], CY, 2 * RING_R[2] + 1, COL_RING);
    s.vLine(CX, CY - RING_R[2], 2 * RING_R[2] + 1, COL_RING);
    s.circle(CX, CY, BUBBLE_R + 4, level ? COL_OK : COL_DIM);

    // Bubble floats to the HIGH side = opposite of in-plane gravity.
    float bx = -fx_ * PX_PER_G;
    float by = -fy_ * PX_PER_G;
    const float d   = sqrtf(bx * bx + by * by);
    const float max = float(RING_R[2] - BUBBLE_R);
    if (d > max) { bx *= max / d; by *= max / d; }
    s.filledCircle(CX + int(bx), CY + int(by), BUBBLE_R, level ? COL_OK : COL_ACCENT);

    // Readout.
    snprintf(line, sizeof(line), "%4.1f", tiltDeg);
    s.text((W - s.textWidth(line, 4)) / 2, 396, line, level ? COL_OK : COL_TEXT, 4);
    s.text(W / 2 + s.textWidth(line, 4) / 2 + 8, 396, "deg", COL_DIM, 2);

    const float degX = atan2f(fx_, fabsf(fz_)) * 57.2958f;
    const float degY = atan2f(fy_, fabsf(fz_)) * 57.2958f;
    snprintf(line, sizeof(line), "X %+5.1f   Y %+5.1f", degX, degY);
    s.text((W - s.textWidth(line, 2)) / 2, 66, line, COL_DIM, 2);

    if (level) {
        s.text((W - s.textWidth("LEVEL!", 2)) / 2, 90, "LEVEL!", COL_OK, 2);
    }
}

} // namespace apps
