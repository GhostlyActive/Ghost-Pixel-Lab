// The cloud punch-through played whenever the view switches between space and
// a planet surface. Puffs start covering the screen, then shrink and race
// outward from the centre, so the new view is revealed rather than cut to.
//
// It belongs to neither view — both play it — so it lives on its own.
#pragma once

#include "board/surface.h"

#include <cstdint>

namespace apps::outer {

// How long the transition runs, in seconds.
inline constexpr float DIVE_TIME = 0.7f;

// `elapsed` counts up from 0; at DIVE_TIME the screen is clear. `sky` is the
// colour the puffs are tinted from, so they match whichever side we land on.
void drawClouds(board::gfx::Surface& s, uint16_t sky, float elapsed);

} // namespace apps::outer
