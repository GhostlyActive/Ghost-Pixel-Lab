// Wireframe 3D cube. Orientation tracks the QMI8658 IMU through a
// complementary filter; without an IMU it falls back to a slow auto-spin.
#pragma once

#include "core/app.h"

namespace apps {

class Cube3D final : public core::App {
public:
    const char* name() const override { return "Cube 3D"; }
    const char* info() const override { return "IMU wireframe cube"; }

    void onEnter() override;
    void update(const core::Input& in, float dt) override;
    void render(board::gfx::Surface& s) override;

private:
    float roll_ = 0, pitch_ = 0, yaw_ = 0;
    float scrollX_ = 0;
};

} // namespace apps
