// Maze Ball: tilt the board to roll a ball through a generated maze to the
// goal. New maze every win. Without an IMU, touch-drag pushes the ball.
#pragma once

#include "core/app.h"
#include <cstdint>

namespace apps {

class MazeBall final : public core::App {
public:
    const char* name() const override { return "Maze Ball"; }
    const char* info() const override { return "tilt the ball home"; }

    void onEnter() override;
    void update(const core::Input& in, float dt) override;
    void render(board::gfx::Surface& s) override;

private:
    static constexpr int COLS = 7;
    static constexpr int ROWS = 8;

    void generate();
    void resetBall();
    void collide();
    uint32_t rnd() { rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5; return rng_; }

    uint8_t  walls_[ROWS][COLS] = {};   // bit 0 = right wall, bit 1 = bottom
    uint32_t rng_   = 0x5EED5EED;
    float    px_ = 0, py_ = 0, vx_ = 0, vy_ = 0;
    float    winTimer_ = 0;             // > 0 while the win flash shows
    int      level_    = 1;
};

} // namespace apps
