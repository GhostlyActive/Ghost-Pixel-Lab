#include "sensor_lab.h"
#include "core/app_manager.h"
#include "core/hw.h"

#include "board/display.h"
#include "board/power.h"

#include <Arduino.h>
#include <cstdio>

namespace apps {

namespace {

using board::gfx::Surface;

constexpr int MARGIN = 14;
constexpr int LINE_H = 20;

constexpr uint16_t COL_ACCENT = 0x07FF;
constexpr uint16_t COL_TEXT   = 0xFFFF;
constexpr uint16_t COL_DIM    = 0x8410;
constexpr uint16_t COL_RULE   = 0x2945;
constexpr uint16_t COL_BAR_BG = 0x10A2;

// Section header with rule; returns the y where content starts.
int section(Surface& s, int y, const char* title, bool present) {
    s.text(MARGIN, y, title, present ? COL_ACCENT : COL_DIM, 2);
    if (!present) {
        const char* miss = "missing";
        s.text(board::display::WIDTH - MARGIN - s.textWidth(miss, 2), y, miss, COL_DIM, 2);
    }
    s.hLine(MARGIN, y + 18, board::display::WIDTH - 2 * MARGIN, COL_RULE);
    return y + 28;
}

} // namespace

void SensorLab::onEnter() {
    slowMs_ = 0;  // force a slow-path refresh on the first frame
}

void SensorLab::update(const core::Input& in, float) {
    touch_ = in;

    if (core::hw::imu) {
        (void)board::imu::readAccel(accel_);
        (void)board::imu::readGyro(gyro_);
    }

    const uint32_t now = millis();
    if (slowMs_ != 0 && now - slowMs_ < 500) return;
    slowMs_ = now;

    if (core::hw::imu) imuTempC_ = board::imu::readTemperatureC();
    if (core::hw::power) {
        battPct_  = board::power::batteryPercent();
        battV_    = board::power::batteryVolts();
        vbusV_    = board::power::vbusMillivolts() / 1000.0f;
        vbusOn_   = board::power::vbusPresent();
        charging_ = board::power::charging();
    }
    timeOk_ = core::hw::rtc && board::rtc::read(time_);
}

void SensorLab::render(Surface& s) {
    const int W = board::display::WIDTH;
    char line[48];

    s.clear(0x0000);
    // Centered so the panel's rounded corners can't clip it.
    s.text((W - s.textWidth("SENSOR LAB", 3)) / 2, 12, "SENSOR LAB", COL_ACCENT, 3);

    int y = 50;

    y = section(s, y, "IMU  QMI8658", core::hw::imu);
    if (core::hw::imu) {
        snprintf(line, sizeof(line), "ACC %+6.2f %+6.2f %+6.2f g",
                 accel_.x, accel_.y, accel_.z);
        s.text(MARGIN, y, line, COL_TEXT, 2); y += LINE_H;
        snprintf(line, sizeof(line), "GYR %+6.1f %+6.1f %+6.1f dps",
                 gyro_.x, gyro_.y, gyro_.z);
        s.text(MARGIN, y, line, COL_TEXT, 2); y += LINE_H;
        snprintf(line, sizeof(line), "TMP %5.1f C", imuTempC_);
        s.text(MARGIN, y, line, COL_TEXT, 2); y += LINE_H;
    }
    y += 12;

    y = section(s, y, "POWER  AXP2101", core::hw::power);
    if (core::hw::power) {
        snprintf(line, sizeof(line), "BAT %3d%%  %.3f V%s",
                 battPct_, battV_, charging_ ? "  CHG" : "");
        s.text(MARGIN, y, line, COL_TEXT, 2); y += LINE_H;
        const int pct = battPct_ < 0 ? 0 : battPct_;
        const uint16_t col = pct > 50 ? 0x07E0 : pct > 20 ? 0xFFE0 : 0xF800;
        s.fillRect(MARGIN, y, 204, 14, COL_BAR_BG);
        s.fillRect(MARGIN + 2, y + 2, 2 * pct, 10, col);
        y += LINE_H;
        snprintf(line, sizeof(line), "VBUS %.2f V%s", vbusV_, vbusOn_ ? "" : "  (off)");
        s.text(MARGIN, y, line, COL_TEXT, 2); y += LINE_H;
    }
    y += 12;

    y = section(s, y, "CLOCK  PCF85063", core::hw::rtc);
    if (core::hw::rtc) {
        if (timeOk_) {
            snprintf(line, sizeof(line), "%04u-%02u-%02u  %02u:%02u:%02u",
                     time_.year, time_.month, time_.day,
                     time_.hour, time_.minute, time_.second);
        } else {
            snprintf(line, sizeof(line), "read failed");
        }
        s.text(MARGIN, y, line, COL_TEXT, 2); y += LINE_H;
    }
    y += 12;

    y = section(s, y, "SYSTEM", true);
    const uint32_t up = millis() / 1000;
    snprintf(line, sizeof(line), "FPS %5.1f   UP %02u:%02u:%02u",
             core::manager::fps(),
             (unsigned)(up / 3600), (unsigned)(up / 60 % 60), (unsigned)(up % 60));
    s.text(MARGIN, y, line, COL_TEXT, 2); y += LINE_H;
    snprintf(line, sizeof(line), "HEAP %u K   PSRAM %u K",
             (unsigned)(ESP.getFreeHeap() / 1024),
             (unsigned)(ESP.getFreePsram() / 1024));
    s.text(MARGIN, y, line, COL_TEXT, 2); y += LINE_H;
    const auto st = core::manager::frameStats();
    snprintf(line, sizeof(line), "FRAME upd%4.1f drw%4.1f shw%4.1f",
             st.updateMs, st.drawMs, st.showMs);
    s.text(MARGIN, y, line, COL_TEXT, 2); y += LINE_H;
    y += 12;

    y = section(s, y, "TOUCH  FT3168", core::hw::touch);
    if (core::hw::touch) {
        if (touch_.pressed) {
            snprintf(line, sizeof(line), "x=%3d  y=%3d", touch_.x, touch_.y);
            s.text(MARGIN, y, line, COL_TEXT, 2);
        } else {
            s.text(MARGIN, y, "touch the screen", COL_DIM, 2);
        }
    }

    // Crosshair on top of everything while touching.
    if (core::hw::touch && touch_.pressed) {
        s.hLine(0, touch_.y, W, COL_ACCENT);
        s.vLine(touch_.x, 0, board::display::HEIGHT, COL_ACCENT);
        s.filledCircle(touch_.x, touch_.y, 6, COL_ACCENT);
    }
}

} // namespace apps
