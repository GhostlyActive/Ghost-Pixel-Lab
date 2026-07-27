// The view from the cockpit in space: starfield, orbit lines, the bodies
// themselves, the navigation reticle and the HUD.
//
// There is no 3D engine under this. Every body is projected to a screen circle
// and drawn as a shaded disc, painter-sorted far to near — which is enough
// because the system is spheres and nothing intersects. The one expensive part,
// a screen-filling planet, is why drawSphere() shades in blocks and avoids a
// per-pixel square root.
#pragma once

#include "ship.h"
#include "board/surface.h"

#include <cstdint>

namespace apps::outer {

class SpaceView {
public:
    // Scatter the starfield over the unit sphere. Stars are directions, not
    // positions — they never move relative to the ship, only rotate.
    void reset();

    // `diveElapsed` >= 0 plays the cloud transition on top; pass a negative
    // value when no transition is running.
    void render(board::gfx::Surface& s, const Ship& ship,
                bool showOrbits, bool padConnected, float diveElapsed);

    // The atmosphere the ship is currently inside, as a colour — the app needs
    // it to tint the transition when dropping to the surface. Valid after a
    // render() call.
    [[nodiscard]] uint16_t sky() const { return sky_; }

private:
    static constexpr int STARS = 80;

    float starX_[STARS] = {}, starY_[STARS] = {}, starZ_[STARS] = {};

    // Cheap xorshift, used for the star field and the reentry sparks. Its
    // state is deliberately not reset with the stars: sparks should not repeat
    // the same pattern on every entry.
    uint32_t rng_ = 0xBEEF77;
    uint32_t rnd() { rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5; return rng_; }

    uint16_t sky_ = 0x0000;
};

} // namespace apps::outer
