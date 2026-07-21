// Disk: what the device is storing on, how full it is, and the one destructive
// button in the whole system — FORMAT, which erases the card and rebuilds the
// /GHOST layout. It asks for a second tap before doing anything.
#pragma once

#include "core/app.h"
#include <cstdint>

namespace apps {

class Disk final : public core::App {
public:
    const char* name() const override { return "Disk"; }
    const char* info() const override { return "storage & format"; }

    void onEnter() override;
    void update(const core::Input& in, float dt) override;
    void render(board::gfx::Surface& s) override;

private:
    void refresh();

    uint64_t total_ = 0, used_ = 0;
    int      programs_ = 0;
    bool     armed_   = false;   // first tap arms, second tap formats
    float    armT_    = 0;       // arming times out
    bool     done_    = false;
    float    refreshT_ = 0;
    bool     pressedBtn_ = false;
};

} // namespace apps
