// Falling sand: classic cellular automaton on a 184x224 grid (2x2 px per
// cell). Touch pours sand, tilting the board changes which way it falls.
#pragma once

#include "core/app.h"
#include <cstdint>

namespace apps {

class Sand final : public core::App {
public:
    const char* name() const override { return "Sand"; }
    const char* info() const override { return "falling sand + tilt"; }

    void onEnter() override;
    void onExit() override;
    void update(const core::Input& in, float dt) override;
    void render(board::gfx::Surface& s) override;

private:
    void stamp(int cx, int cy);
    void stepVertical(int dy);
    void stepHorizontal(int dx);
    uint32_t rnd() { rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5; return rng_; }

    uint8_t* g_       = nullptr;   // grid, palette indices
    uint32_t rng_     = 0xC0FFEE11;
    int      dirX_    = 0, dirY_ = 1;   // current gravity (cardinal)
    int      colorIdx_ = 1;
    float    colorTimer_ = 0;
};

} // namespace apps
