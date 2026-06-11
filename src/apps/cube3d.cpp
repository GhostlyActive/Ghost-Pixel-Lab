#include "cube3d.h"
#include "core/app_manager.h"
#include "core/hw.h"

#include "board/display.h"
#include "board/imu.h"
#include "board/power.h"

#include <cmath>
#include <cstdio>

namespace apps {

namespace {

using board::gfx::Surface;

struct V3 { float x, y, z; };

constexpr V3 CUBE_V[8] = {
    {-1,-1,-1},{ 1,-1,-1},{ 1, 1,-1},{-1, 1,-1},
    {-1,-1, 1},{ 1,-1, 1},{ 1, 1, 1},{-1, 1, 1},
};
constexpr uint8_t CUBE_E[12][2] = {
    {0,1},{1,2},{2,3},{3,0}, {4,5},{5,6},{6,7},{7,4},
    {0,4},{1,5},{2,6},{3,7},
};
constexpr uint16_t EDGE_COL[12] = {
    0xF800,0xF800,0xF800,0xF800,
    0x07E0,0x07E0,0x07E0,0x07E0,
    0x001F,0xFFE0,0xF81F,0x07FF,
};

constexpr char SCROLL_TEXT[] =
    "  ESP32-S3 Touch AMOLED 1.8   *   SH8601 QSPI   *   368x448 RGB565"
    "   *   IMU-driven 3D demo   *   ";
constexpr float SCROLL_SPEED = 120.0f;  // px/s

inline void rotInPlane(float& a, float& b, float c, float s) {
    const float na = a * c - b * s;
    const float nb = a * s + b * c;
    a = na; b = nb;
}

} // namespace

void Cube3D::onEnter() {
    // Orientation survives between visits; just restart the marquee.
    scrollX_ = 0;
}

void Cube3D::update(const core::Input&, float dt) {
    scrollX_ += SCROLL_SPEED * dt;

    if (!core::hw::imu) {
        roll_  += 0.9f * dt;
        pitch_ += 1.3f * dt;
        yaw_   += 0.5f * dt;
        return;
    }
    if (dt <= 0.0f) return;

    board::imu::Vec3 accel, gyro;
    if (board::imu::readGyro(gyro)) {
        constexpr float kDegToRad = 0.0174532925f;
        roll_  += gyro.x * dt * kDegToRad;
        pitch_ += gyro.y * dt * kDegToRad;
        yaw_   += gyro.z * dt * kDegToRad;
    }
    if (board::imu::readAccel(accel)) {
        const float aPitch = atan2f(accel.y, sqrtf(accel.x * accel.x + accel.z * accel.z));
        const float aRoll  = atan2f(-accel.x, accel.z);
        constexpr float ALPHA = 0.98f;
        roll_  = ALPHA * roll_  + (1.0f - ALPHA) * aRoll;
        pitch_ = ALPHA * pitch_ + (1.0f - ALPHA) * aPitch;
    }
}

void Cube3D::render(Surface& s) {
    s.clear(0x0000);

    // Cube.
    const float cax = cosf(pitch_), sax = sinf(pitch_);
    const float cay = cosf(roll_),  say = sinf(roll_);
    const float caz = cosf(yaw_),   saz = sinf(yaw_);
    constexpr float SCALE = 105.0f;
    constexpr float CAM_Z = 4.0f;
    const int cx = board::display::WIDTH  / 2;
    const int cy = board::display::HEIGHT / 2;

    int sx[8], sy[8];
    for (int i = 0; i < 8; ++i) {
        float x = CUBE_V[i].x, y = CUBE_V[i].y, z = CUBE_V[i].z;
        rotInPlane(y, z, cax, sax);
        rotInPlane(x, z, cay, say);
        rotInPlane(x, y, caz, saz);
        const float persp = CAM_Z / (CAM_Z + z);
        sx[i] = cx + static_cast<int>(lroundf(x * SCALE * persp));
        sy[i] = cy + static_cast<int>(lroundf(y * SCALE * persp));
    }
    for (int i = 0; i < 12; ++i) {
        s.thickLine(sx[CUBE_E[i][0]], sy[CUBE_E[i][0]],
                    sx[CUBE_E[i][1]], sy[CUBE_E[i][1]], EDGE_COL[i]);
    }
    for (int i = 0; i < 8; ++i) {
        s.filledCircle(sx[i], sy[i], 4, 0xFFFF);
    }

    // HUD, centered so the panel's rounded corners can't clip it.
    char top[64];
    if (core::hw::power) {
        snprintf(top, sizeof(top), "%4.1f FPS   %d%%   %.2fV",
                 core::manager::fps(), board::power::batteryPercent(),
                 board::power::batteryVolts());
    } else {
        snprintf(top, sizeof(top), "%4.1f FPS", core::manager::fps());
    }
    s.text((board::display::WIDTH - s.textWidth(top, 2)) / 2, 10, top, 0xFFFF, 2);

    // Marquee.
    constexpr int TEXT_SCALE = 2;
    const int step    = board::font::advance(TEXT_SCALE);
    const int widthPx = static_cast<int>(sizeof(SCROLL_TEXT) - 1) * step;
    const int y       = board::display::HEIGHT - board::font::CHAR_H * TEXT_SCALE - 8;
    if (scrollX_ >= static_cast<float>(widthPx)) {
        scrollX_ = fmodf(scrollX_, static_cast<float>(widthPx));
    }
    int x = -static_cast<int>(scrollX_);
    while (x < board::display::WIDTH) {
        s.text(x, y, SCROLL_TEXT, 0x07FF, TEXT_SCALE);
        x += widthPx;
    }
}

} // namespace apps
