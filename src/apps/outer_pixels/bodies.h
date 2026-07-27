// The solar system: one table of bodies, and the orbital mechanics that move
// them. `parent` generalises the hierarchy — a moon orbits a planet, a planet
// or comet orbits the sun, the sun sits at the origin — so a single forward
// pass over the table updates everything, as long as a parent is listed before
// its children.
//
// Pure arithmetic: no display, no Arduino, no app state. That is what lets the
// orbits be stepped and checked on a PC.
#pragma once

#include <cstdint>

namespace apps::outer {

struct Body {
    float    x, y, z;               // live position, recomputed every frame
    float    radius, gm;            // gm = gravitational parameter (0 = no pull)
    uint16_t col, atmo;
    bool     sun, comet;
    int8_t   parent;                // body it orbits (-1 = fixed at the origin)
    const char* name;
    float    orbR, orbW, inc, node, phase;   // circular orbit, inclined + rotated
};

inline constexpr int COUNT = 15;    // bodies.cpp static_asserts the table matches
inline constexpr int SUN   = 0;     // index of the light source

// The table itself. Mutable because x/y/z are recomputed by update(); the
// orbital parameters beside them are never written.
extern Body BODIES[COUNT];

// Position on a body's inclined circular orbit at angle a, relative to its
// parent. Exposed because the renderer traces orbit lines with it.
void orbitPoint(const Body& b, float a, float& X, float& Y, float& Z);

// World-space velocity of a body: its parent's velocity plus its own orbital
// velocity. A ship that lands inherits this, so it rides along with the planet.
void bodyVelocity(int idx, float t, float& vX, float& vY, float& vZ);

// Recompute every body's position for simulation time t.
void update(float t);

} // namespace apps::outer
