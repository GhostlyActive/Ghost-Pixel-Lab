// Echo: hold the button to record from the MEMS mic, release to play it
// back through the speaker. Mic + speaker smoke test with waveform view.
#pragma once

#include "core/app.h"
#include <cstdint>

namespace apps {

class Echo final : public core::App {
public:
    const char* name() const override { return "Echo"; }
    const char* info() const override { return "mic record & replay"; }

    void onEnter() override;
    void onExit() override;
    void update(const core::Input& in, float dt) override;
    void render(board::gfx::Surface& s) override;

private:
    enum class State { NoAudio, NoMem, Idle, Recording, Playing };

    void startRecording();
    void startPlayback();
    void stopToIdle();

    State    state_   = State::Idle;
    int16_t* buf_     = nullptr;   // PSRAM, MAX_SAMPLES
    size_t   recLen_  = 0;         // valid samples in buf_
    size_t   playPos_ = 0;
    float    peak_    = 0;         // 0..1 level meter while recording
};

} // namespace apps
