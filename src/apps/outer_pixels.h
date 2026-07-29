// Outer Pixels: a small space-flight sim. Planets orbit the sun; fly with the
// gamepad, drift onto a surface to land, and launch again. Atmosphere tints the
// sky and stars on descent; reentry heat glows at speed. Drop low enough over a
// planet and the view becomes a voxel landscape you fly through.
//
// This file is the core::App shell only: it owns the pieces in
// src/apps/outer_pixels/, folds whichever input device is present into one
// Controls struct, and drives the terrain generator. Everything else — orbits,
// flight model, both renderers — lives in that folder and depends on nothing
// from core::, so it compiles and is tested on a PC.
//
// Controls (Xbox pad): left stick = steer, RT = thrust, LT = brake,
// A = pick the planet under the crosshair (or launch when landed),
// Y = toggle orbit lines, right stick = roll.
//
// By touch: hold a finger anywhere and drag to steer — holding is also the
// throttle, winding thrust up over about a second, and lifting the finger
// brakes the ship to a standstill. The bottom-left corner is A.
//
// Or type: arrow keys steer, Q/E roll, W/S thrust and brake, A picks/launches,
// Y orbits, B leaves a surface. Keys arrive from the serial console or a BLE
// keyboard; each press is a decaying impulse, so holding a key (auto-repeat)
// flies smoothly. Taking the throttle by key disarms the touch brake, so the
// two schemes do not fight. The controller is searched for automatically.
#pragma once

#include "core/app.h"
#include "apps/outer_pixels/ship.h"
#include "apps/outer_pixels/terrain.h"
#include "apps/outer_pixels/space_view.h"
#include "apps/outer_pixels/surface_view.h"

#include <cstdint>

namespace apps {

class Outer_Pixels final : public core::App {
public:
    const char* name() const override { return "Outer Pixels"; }
    const char* info() const override { return "explore a solar system"; }

    void onEnter() override;
    void onExit() override;
    void update(const core::Input& in, float dt) override;
    void render(board::gfx::Surface& s) override;

private:
    outer::Ship        ship_;
    outer::Terrain     terrain_;
    outer::SpaceView   space_;
    outer::SurfaceView surface_;

    float t_ = 0;                       // simulation time, drives the orbits
    int   flyingOver_ = -1;             // body index in surface mode, -1 = space
    bool  showOrbits_ = false;

    // Build the Controls for this frame out of pad, touch and keyboard.
    outer::Controls readInput(const core::Input& in, float dt, bool surfaceMode);
    bool  prevA_ = false, prevY_ = false;

    // Touch throttle: a finger held anywhere but the A corner winds thrust up
    // over about a second, and lifting it brakes the ship to a standstill.
    // Armed only once a finger has actually driven it, so flying on the keys
    // keeps its old coast-and-drift feel instead of stopping dead.
    float touchThrust_   = 0;
    bool  touchThrottle_ = false;

    // Keyboard fallback while no pad is connected: key presses become impulses
    // on a virtual stick that decay over a fraction of a second, because a
    // serial console never reports key releases — only presses.
    float kyaw_ = 0, kpitch_ = 0, kroll_ = 0, kthrust_ = 0;
    bool  kA_ = false, kY_ = false, kB_ = false;
    void  pollKeys(float dt);

    // Terrain buffers live in PSRAM and outlive a visit to the app, so coming
    // back to the same planet needs no rebuild.
    uint8_t*  heights_ = nullptr;
    uint16_t* colors_  = nullptr;

    // The tile is built on a background task while you descend, so entering
    // surface mode never hitches. reqPlanet_ is the request, Terrain::held()
    // the answer.
    volatile int reqPlanet_ = -1;
    void*        genTask_   = nullptr;
    void         genLoop();
    static void  genTaskTramp(void* self);

    // Cloud punch-through between the two views.
    bool  diving_ = false;
    float diveT_  = 0;
};

} // namespace apps
