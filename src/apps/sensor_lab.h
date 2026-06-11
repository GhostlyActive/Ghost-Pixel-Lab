// Live dashboard for every on-board peripheral: IMU, power, RTC, touch,
// plus FPS / heap / PSRAM. Handy first stop when bringing up new hardware.
#pragma once

#include "core/app.h"
#include "board/imu.h"
#include "board/rtc.h"

#include <cstdint>

namespace apps {

class SensorLab final : public core::App {
public:
    const char* name() const override { return "Sensor Lab"; }
    const char* info() const override { return "hardware dashboard"; }

    void onEnter() override;
    void update(const core::Input& in, float dt) override;
    void render(board::gfx::Surface& s) override;

private:
    // Fast path, refreshed every frame.
    board::imu::Vec3 accel_{}, gyro_{};
    core::Input      touch_{};

    // Slow path, refreshed at 2 Hz to keep I2C traffic down.
    uint32_t slowMs_   = 0;
    float    imuTempC_ = 0;
    int      battPct_  = -1;
    float    battV_    = 0;
    float    vbusV_    = 0;
    bool     vbusOn_   = false;
    bool     charging_ = false;
    bool     timeOk_   = false;
    board::rtc::DateTime time_{};
};

} // namespace apps
