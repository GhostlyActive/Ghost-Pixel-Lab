// Piano: one touch octave (C4..C5 plus black keys), monophonic sine synth
// with attack/release envelope through the speaker. Slide for glissando.
#pragma once

#include "core/app.h"

namespace apps {

class Piano final : public core::App {
public:
    const char* name() const override { return "Piano"; }
    const char* info() const override { return "touch synth keys"; }

    void onEnter() override;
    void onExit() override;
    void update(const core::Input& in, float dt) override;
    void render(board::gfx::Surface& s) override;

private:
    bool  audioOk_ = false;
    int   note_    = -1;   // 0..7 white, 8..12 black, -1 none
    float freq_    = 440.0f;
    float phase_   = 0;
    float env_     = 0;
};

} // namespace apps
