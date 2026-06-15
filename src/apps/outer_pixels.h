// Outer Pixels: a small space-flight sim. Planets orbit the sun; fly with the
// gamepad, drift onto a surface to land, and launch again. Atmosphere tints
// the sky and stars on descent; reentry heat glows at speed. No engine — all
// the simulation and rendering lives in this app.
//
// Controls (Xbox pad): left stick = steer, RT = thrust, LT = brake,
// A = pick the planet under the crosshair (or launch when landed),
// Y = toggle orbit lines, right stick = roll. Without a pad: drag to steer,
// corners thrust/launch. The controller is searched for automatically.
#pragma once

#include "core/app.h"
#include "core/pad.h"
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
    float px_ = 0, py_ = 0, pz_ = 0;     // ship position
    float vx_ = 0, vy_ = 0, vz_ = 0;     // ship velocity
    // Orientation as an orthonormal body frame (true 6DOF: forward/right/up).
    float fwd_[3]   = {0, 0, 1};
    float right_[3] = {1, 0, 0};
    float up_[3]    = {0, 1, 0};
    int   landed_ = -1;                  // planet index, or -1 while flying
    float landOX_ = 0, landOY_ = 0, landOZ_ = 0;  // surface offset while landed
    int   selected_ = -1;                // navigation target (A picks it)
    bool  showOrbits_ = false;
    bool  prevA_ = false, prevY_ = false;
    float t_ = 0;                        // sim time (drives orbits)

    float starX_[80], starY_[80], starZ_[80];
    uint32_t rng_ = 0xBEEF77;
    uint32_t rnd() { rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5; return rng_; }

    // --- surface (voxel heightmap) mode ---
    // When you drop low over a planet the view switches to a Comanche-style
    // voxel terrain you fly through (procedural mountains). All of it lives in
    // this app, grouped in one clearly marked section of outer_pixels.cpp.
    int       surface_ = -1;            // planet index in surface mode, -1 = space
    float     sx_ = 0, sy_ = 0;         // voxel camera position on the terrain
    float     salt_ = 0;                // altitude above the terrain plane
    float     syaw_ = 0, spitch_ = 0;   // heading / nose angle
    uint8_t*  hmap_ = nullptr;          // procedural heightmap tile (PSRAM)
    uint16_t* cmap_ = nullptr;          // matching color tile (PSRAM)

    // The terrain tile is built in the background while you approach (so the
    // switch to surface mode is instant) and cached per planet.
    volatile int reqPlanet_  = -1;      // planet the gen task should build
    volatile int donePlanet_ = -1;      // planet currently held in hmap_/cmap_
    void*        genTask_     = nullptr;

    void enterSurface(int planet);
    void exitSurface();
    void updateSurface(const core::Input& in, float dt);
    void renderSurface(board::gfx::Surface& s);
    void genLoop();                     // background terrain-generation loop
    static void genTaskTramp(void* self);
};

} // namespace apps
