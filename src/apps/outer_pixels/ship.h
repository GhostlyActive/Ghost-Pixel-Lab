// The ship: a 6-DOF flight model in the solar system's world space.
//
// Orientation is three orthonormal axis vectors rather than Euler angles, so
// steering is always relative to the ship's current attitude — roll ninety
// degrees and "pitch up" still means toward the top of the screen. The axes
// are re-orthonormalised every frame, which is what keeps numeric drift from
// slowly shearing the frame.
//
// Every body pulls on the ship at once (no patched conics), and touching one
// slowly enough lands on it; too fast bounces. A landed ship rides its planet's
// orbital velocity, so launching from a moving world hands that speed back.
//
// Pure arithmetic against bodies.h — no display, no input device, no Arduino.
#pragma once

#include "controls.h"

namespace apps::outer {

class Ship {
public:
    void reset();

    // One frame of flight. `t` is simulation time, needed because landing and
    // launching have to match the planet's orbital velocity.
    void step(const Controls& c, float dt, float t);

    // Nearest body with a solid surface, and the altitude above it. Returns -1
    // when there is none. The app uses this to decide when to build terrain and
    // when to drop into surface mode.
    int nearestLandable(float& altitude) const;

    // Placed above a planet after climbing out of surface mode, pointing away
    // from it with some outward speed.
    void placeAbove(int body);

    // --- state the renderer reads ------------------------------------------
    float x = 0, y = 0, z = 0;          // position
    float vx = 0, vy = 0, vz = 0;       // velocity
    float fwd[3]   = {0, 0, 1};         // orthonormal body frame
    float right[3] = {1, 0, 0};
    float up[3]    = {0, 1, 0};

    int   landedOn = -1;                // body index while sitting on one, else -1
    int   target   = -1;                // navigation target picked with A, or -1

    [[nodiscard]] float speed() const;

private:
    void   integrate(float thrust, float dt);
    void   resolveContact(float t);
    int    bodyUnderCrosshair() const;   // ray-sphere, falling back to a narrow cone

    // Where on the planet we touched down, in that planet's frame.
    float landX_ = 0, landY_ = 0, landZ_ = 0;
};

} // namespace apps::outer
