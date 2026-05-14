// Rotating 3D cube with a scrolling marquee. The cube's orientation tracks
// the on-board QMI8658 IMU via a complementary filter, so tilting the board
// tilts the cube.
//
// All hardware lives behind the board::* modules in src/board/. This file
// is just the demo: framebuffer drawing + a render loop.
#include <Arduino.h>
#include <Wire.h>
#include "HWCDC.h"
#include <math.h>

#include "board/pins.h"
#include "board/i2c.h"
#include "board/surface.h"
#include "board/display.h"
#include "board/expander.h"
#include "board/imu.h"
#include "board/power.h"

namespace bp = board::pins;
using board::gfx::Surface;

HWCDC USBSerial;

// --- cube geometry -------------------------------------------------------
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

static inline void rotInPlane(float& a, float& b, float c, float s) {
    const float na = a * c - b * s;
    const float nb = a * s + b * c;
    a = na; b = nb;
}

// --- state ---------------------------------------------------------------
struct Orientation { float roll = 0, pitch = 0, yaw = 0; };

Orientation orient;
bool imuPresent = false;
bool pmuPresent = false;

uint32_t lastImuMicros = 0;
uint32_t frameCount    = 0;
uint32_t fpsWindowMs   = 0;
float    fps           = 0.0f;
int      scrollX       = 0;

constexpr char SCROLL_TEXT[] =
    "  ESP32-S3 Touch AMOLED 1.8   *   SH8601 QSPI   *   368x448 RGB565"
    "   *   IMU-driven 3D demo   *   ";

// --- sensor fusion -------------------------------------------------------
static void updateOrientation() {
    if (!imuPresent) {
        const float t = millis() * 0.001f;
        orient.roll  = t * 0.9f;
        orient.pitch = t * 1.3f;
        orient.yaw   = t * 0.5f;
        return;
    }

    board::imu::Vec3 accel, gyro;
    const bool okA = board::imu::readAccel(accel);
    const bool okG = board::imu::readGyro(gyro);

    const uint32_t now = micros();
    const float dt = (now - lastImuMicros) * 1e-6f;
    lastImuMicros = now;
    if (dt <= 0.0f || dt > 0.2f) return;

    if (okG) {
        constexpr float kDegToRad = 0.0174532925f;
        orient.roll  += gyro.x * dt * kDegToRad;
        orient.pitch += gyro.y * dt * kDegToRad;
        orient.yaw   += gyro.z * dt * kDegToRad;
    }
    if (okA) {
        const float aPitch = atan2f(accel.y, sqrtf(accel.x * accel.x + accel.z * accel.z));
        const float aRoll  = atan2f(-accel.x, accel.z);
        constexpr float ALPHA = 0.98f;
        orient.roll  = ALPHA * orient.roll  + (1.0f - ALPHA) * aRoll;
        orient.pitch = ALPHA * orient.pitch + (1.0f - ALPHA) * aPitch;
    }
}

// --- rendering -----------------------------------------------------------
static void drawCube(Surface& s) {
    const float cax = cosf(orient.pitch), sax = sinf(orient.pitch);
    const float cay = cosf(orient.roll),  say = sinf(orient.roll);
    const float caz = cosf(orient.yaw),   saz = sinf(orient.yaw);
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
}

static void drawHUD(Surface& s) {
    char top[64];
    if (pmuPresent) {
        snprintf(top, sizeof(top), "%4.1f FPS   %d%%   %.2fV",
                 fps, board::power::batteryPercent(), board::power::batteryVolts());
    } else {
        snprintf(top, sizeof(top), "%4.1f FPS", fps);
    }
    s.text(10, 8, top, 0xFFFF, 2);

    constexpr int SCALE = 2;
    const int step    = board::font::advance(SCALE);
    const int widthPx = static_cast<int>(sizeof(SCROLL_TEXT) - 1) * step;
    const int y       = board::display::HEIGHT - board::font::CHAR_H * SCALE - 8;
    int x = -scrollX;
    while (x < board::display::WIDTH) {
        s.text(x, y, SCROLL_TEXT, 0x07FF, SCALE);
        x += widthPx;
    }
    scrollX = (scrollX + 2) % widthPx;
}

// --- setup / loop --------------------------------------------------------
void setup() {
    USBSerial.begin(115200);
    delay(300);
    USBSerial.println("\n[boot] starting");

    if (!board::display::begin()) {
        USBSerial.println("[boot] display init failed");
        while (true) delay(1000);
    }

    Wire.begin(bp::I2C_SDA, bp::I2C_SCL, 400000);
    board::expander::begin();

    imuPresent = board::imu::begin();
    pmuPresent = board::power::begin();
    USBSerial.printf("[boot] imu=%d pmu=%d\n", imuPresent, pmuPresent);

    lastImuMicros = micros();
    fpsWindowMs   = millis();
}

void loop() {
    updateOrientation();

    Surface s = board::display::canvas();
    s.clear(0x0000);
    drawCube(s);
    drawHUD(s);
    board::display::present();

    ++frameCount;
    const uint32_t now = millis();
    const uint32_t dt  = now - fpsWindowMs;
    if (dt >= 500) {
        fps = frameCount * 1000.0f / static_cast<float>(dt);
        frameCount  = 0;
        fpsWindowMs = now;
        USBSerial.printf("fps=%.1f\n", fps);
    }
}
