#include "apps/outer_pixels.h"
#include "apps/outer_pixels/bodies.h"
#include "apps/outer_pixels/clouds.h"
#include "core/pad.h"
#include "core/keyboard.h"
#include "board/display.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cmath>

namespace apps {

using outer::Controls;

namespace {
constexpr int W = board::display::WIDTH;
constexpr int H = board::display::HEIGHT;

// Altitudes, in world units above a planet's surface.
constexpr float SURFACE_ENTER = 16.0f;   // drop below this and the view switches
constexpr float SURFACE_PREP  = 90.0f;   // start building the terrain here

constexpr float TOUCH_RAMP_S = 1.0f;     // finger held this long = full thrust
}

void Outer_Pixels::onEnter() {
    core::pad::begin();             // start searching for an Xbox controller (background)
    core::keyboard::beginSerial();  // typed keys as fallback; leaves BLE to the pad

    kyaw_ = kpitch_ = kroll_ = kthrust_ = 0;
    kA_ = kY_ = kB_ = false;
    prevA_ = prevY_ = false;
    touchThrust_ = 0; touchThrottle_ = false;

    t_ = 0;
    outer::update(t_);
    ship_.reset();
    space_.reset();
    showOrbits_ = false;
    flyingOver_ = -1;
    diving_ = false; diveT_ = 0;

    // Allocate the terrain buffers and start the generator once; both survive
    // leaving the app, so a planet visited before is still cached.
    if (!heights_)
        heights_ = static_cast<uint8_t*>(
            heap_caps_malloc(outer::Terrain::HEIGHT_BYTES, MALLOC_CAP_SPIRAM));
    if (!colors_)
        colors_ = static_cast<uint16_t*>(
            heap_caps_malloc(outer::Terrain::COLOR_BYTES, MALLOC_CAP_SPIRAM));
    terrain_.attach(heights_, colors_);

    if (!genTask_ && terrain_.attached())
        xTaskCreatePinnedToCore(&Outer_Pixels::genTaskTramp, "terrain", 4096, this, 2,
                                reinterpret_cast<TaskHandle_t*>(&genTask_), 0);
}

void Outer_Pixels::onExit() {
    flyingOver_ = -1;   // keep the tile and the generator alive as a cache
}

// Background task: build whatever planet was last requested, then park. It runs
// while you are still flying down, which is what makes the switch instant.
void Outer_Pixels::genTaskTramp(void* self) {
    static_cast<Outer_Pixels*>(self)->genLoop();
}
void Outer_Pixels::genLoop() {
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        while (reqPlanet_ != terrain_.held()) {
            const int p = reqPlanet_;
            if (p < 0 || !terrain_.attached()) break;
            terrain_.generate(p, outer::BODIES[p].col);
        }
    }
}

// Keys arrive from the serial console or a BLE keyboard, whichever is
// delivering. Impulses are the honest model: a console reports presses only,
// never releases, so a held key (auto-repeat) has to read as a stick pushed
// and slowly returning to centre.
void Outer_Pixels::pollKeys(float dt) {
    const float steerDecay  = expf(-6.0f * dt);
    const float thrustDecay = expf(-2.5f * dt);   // slower: one tap = a burst
    kyaw_ *= steerDecay; kpitch_ *= steerDecay; kroll_ *= steerDecay;
    kthrust_ *= thrustDecay;
    kA_ = kY_ = kB_ = false;

    auto bump = [](float& v, float d) {
        v += d;
        if (v > 1) v = 1; else if (v < -1) v = -1;
    };
    uint8_t k;
    while (core::keyboard::next(k)) {
        switch (k) {
        case 0x9D: bump(kyaw_,  -0.55f); break;   // cursor left
        case 0x1D: bump(kyaw_,   0.55f); break;   // cursor right
        case 0x91: bump(kpitch_, -0.55f); break;  // cursor up = nose up / climb
        case 0x11: bump(kpitch_,  0.55f); break;  // cursor down
        case 'Q': case 'q': bump(kroll_, -0.55f); break;
        case 'E': case 'e': bump(kroll_,  0.55f); break;
        // Taking the throttle by key hands flying back to the keyboard, so the
        // touch brake stops fighting it.
        case 'W': case 'w': bump(kthrust_,  0.7f); touchThrottle_ = false; break;
        case 'S': case 's': bump(kthrust_, -0.7f); touchThrottle_ = false; break;
        case 'A': case 'a': case ' ': kA_ = true; break;    // pick / launch
        case 'Y': case 'y': kY_ = true; break;              // orbit lines
        case 'B': case 'b': kB_ = true; break;              // leave the surface
        default: break;
        }
    }
}

// Fold pad, touch and keyboard into one set of axes. The two modes steer
// differently enough to need their own mapping: in space the stick pitches the
// nose, on a surface it climbs.
Controls Outer_Pixels::readInput(const core::Input& in, float dt, bool surfaceMode) {
    Controls c;
    bool aBtn = false, yBtn = false;

    const auto pad = core::pad::state();
    if (pad.connected) {
        c.yaw    = pad.lx;
        c.pitch  = pad.ly;
        c.roll   = surfaceMode ? 0.0f : pad.rx;
        c.thrust = pad.rt - pad.lt;
        aBtn     = pad.a;
        yBtn     = pad.y;
        c.leave  = pad.b;
    } else if (surfaceMode) {
        if (in.pressed) {
            c.yaw   = (in.x - in.startX) / 120.0f;
            c.pitch = (in.startY - in.y) / 120.0f;   // drag up = climb
        }
        // kpitch_ is negative for nose-up, which is a climb here.
        c.yaw    += kyaw_;
        c.pitch  -= kpitch_;
        c.thrust += kthrust_;
        c.leave   = kB_;
    } else {
        // A held finger steers by how far it has dragged and is the throttle at
        // the same time; lifting it brakes. The bottom-left corner stays the A
        // button, so tapping to pick a planet does not also accelerate.
        const bool aCorner = in.pressed && in.x < 110 && in.y > H - 110;
        if (in.pressed && !aCorner) {
            c.yaw   = (in.x - in.startX) / 120.0f;
            c.pitch = (in.y - in.startY) / 120.0f;
            touchThrust_ += dt / TOUCH_RAMP_S;
            if (touchThrust_ > 1) touchThrust_ = 1;
            touchThrottle_ = true;
        } else if (touchThrottle_) {
            touchThrust_ = 0;
            c.brake = 1.0f;
        }
        aBtn = aCorner;

        c.yaw += kyaw_; c.pitch += kpitch_; c.roll += kroll_;
        c.thrust += touchThrust_ + kthrust_;
        if (c.thrust > 1) c.thrust = 1; else if (c.thrust < -1) c.thrust = -1;
        aBtn = aBtn || kA_;
        yBtn = yBtn || kY_;
    }

    // Edge-trigger the buttons here so the flight model only ever sees the
    // frame a press happened on.
    c.pick   = aBtn && !prevA_;
    c.orbits = yBtn && !prevY_;
    prevA_ = aBtn;
    prevY_ = yBtn;
    return c;
}

void Outer_Pixels::update(const core::Input& in, float dt) {
    if (dt <= 0) return;
    if (dt > 0.05f) dt = 0.05f;

    pollKeys(dt);
    if (diving_) { diveT_ += dt; if (diveT_ >= outer::DIVE_TIME) diving_ = false; }

    if (flyingOver_ >= 0) {
        const Controls c = readInput(in, dt, true);
        if (!surface_.update(c, dt, terrain_)) {
            // Climbing out: pop the ship above the planet and punch back up
            // through the cloud layer.
            ship_.placeAbove(flyingOver_);
            flyingOver_ = -1;
            diving_ = true; diveT_ = 0;
        }
        return;
    }

    t_ += dt;
    outer::update(t_);

    const Controls c = readInput(in, dt, false);
    if (c.orbits) showOrbits_ = !showOrbits_;
    ship_.step(c, dt, t_);

    if (ship_.landedOn >= 0) return;

    // Approaching a planet: request its terrain early so the tile is finished
    // by the time we are low enough to drop in. Tiles are cached per planet, so
    // a second visit skips this entirely.
    float alt = 0;
    const int nearest = ship_.nearestLandable(alt);
    if (nearest < 0) return;

    if (alt < SURFACE_PREP && reqPlanet_ != nearest && terrain_.held() != nearest) {
        reqPlanet_ = nearest;
        if (genTask_) xTaskNotifyGive(static_cast<TaskHandle_t>(genTask_));
    }
    if (alt < SURFACE_ENTER && terrain_.held() == nearest) {
        flyingOver_ = nearest;
        surface_.enter();
        diving_ = true; diveT_ = 0;
    }
}

void Outer_Pixels::render(board::gfx::Surface& s) {
    const bool pad = core::pad::connected();
    const float dive = diving_ ? diveT_ : -1.0f;

    if (flyingOver_ >= 0) {
        const outer::Body& planet = outer::BODIES[flyingOver_];
        surface_.render(s, terrain_, planet);
        if (diving_) outer::drawClouds(s, planet.atmo, diveT_);
        return;
    }
    space_.render(s, ship_, showOrbits_, pad, dive);
}

} // namespace apps
