// Ghost BASIC: a Commodore-64-style computer you program in BASIC. This is the
// core::App shell — it owns the subsystems in src/apps/ghost_basic/ and wires
// them to the frame loop. Everything machine-specific lives in that subfolder
// and depends on nothing from core:: beyond this file, so the machine can later
// be lifted into its own standalone firmware.
//
// Input arrives over a Bluetooth keyboard (core::keyboard); there is
// deliberately no on-screen keyboard. The BOOT key acts as RUN/STOP.
#pragma once

#include "core/app.h"
#include "apps/ghost_basic/screen.h"
#include "apps/ghost_basic/basic.h"
#include "apps/ghost_basic/editor.h"
#include "apps/ghost_basic/sid.h"

#include <cstdint>
#include <string>

namespace apps {

class GhostBasic final : public core::App {
public:
    const char* name() const override { return "Ghost BASIC"; }
    const char* info() const override { return "BASIC computer"; }

    bool capturesBackButton() const override { return true; }  // BOOT = RUN/STOP

    void onEnter() override;
    void onExit() override;
    void update(const core::Input& in, float dt) override;
    void render(board::gfx::Surface& s) override;

private:
    void routeKey(uint8_t k);

    ghost::Screen screen_;
    ghost::Basic  basic_{screen_};
    ghost::Editor editor_{screen_};
    ghost::Sid    sid_;
    bool          audioOk_    = false;
    bool          speakerOn_  = false;
    std::string inputLine_;         // collects the line while BASIC waits on INPUT
    float       blinkT_   = 0;
    bool        cursorOn_ = true;
};

} // namespace apps
