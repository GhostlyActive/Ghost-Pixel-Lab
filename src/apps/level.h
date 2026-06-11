// Level: a spirit level / bubble level driven by the accelerometer. Bubble
// floats to the high side, rings mark 5/10 degrees, readout in degrees.
#pragma once

#include "core/app.h"

namespace apps {

class Level final : public core::App {
public:
    const char* name() const override { return "Level"; }
    const char* info() const override { return "IMU spirit level"; }

    void onEnter() override;
    void update(const core::Input& in, float dt) override;
    void render(board::gfx::Surface& s) override;

private:
    float fx_ = 0, fy_ = 0, fz_ = 1;   // low-passed gravity, screen frame
    bool  haveImu_ = false;
};

} // namespace apps
