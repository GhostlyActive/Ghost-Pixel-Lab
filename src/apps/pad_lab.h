// Pad Lab: live view of an Xbox controller over BLE — sticks, triggers,
// every button, battery. Hold A to test rumble. The Sensor Lab for gamepads.
#pragma once

#include "core/app.h"
#include "core/pad.h"

namespace apps {

class PadLab final : public core::App {
public:
    const char* name() const override { return "Pad Lab"; }
    const char* info() const override { return "Xbox controller test"; }

    void onEnter() override;
    void update(const core::Input& in, float dt) override;
    void render(board::gfx::Surface& s) override;

private:
    core::pad::State st_{};
    bool bleOk_ = false;
    bool prevA_ = false;
};

} // namespace apps
