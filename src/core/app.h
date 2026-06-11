// App: the unit of "one experiment" in this repo. The app manager owns a
// registry of these, runs exactly one at a time and feeds it input plus a
// drawing surface every frame.
//
// Lifecycle: onEnter() -> update()/render() once per frame -> onExit().
// Apps keep their own state; all hardware access goes through board::*.
#pragma once

#include "board/surface.h"
#include <cstdint>

namespace core {

// One frame of touch input, sampled once per tick by the app manager so
// apps never talk to the touch controller themselves.
struct Input {
    bool    pressed      = false;   // finger is down this frame
    bool    justPressed  = false;   // went down this frame
    bool    justReleased = false;   // went up this frame
    int16_t x = 0, y = 0;           // current position (last one once released)
    int16_t startX = 0, startY = 0; // where the current press began
};

class App {
public:
    virtual ~App() = default;

    virtual const char* name() const = 0;            // shown in the menu
    virtual const char* info() const { return ""; }  // one-line menu subtitle

    virtual void onEnter() {}
    virtual void onExit() {}

    // dt is seconds since the previous frame, clamped; 0 after a stall.
    virtual void update(const Input& in, float dt) = 0;

    // Must redraw the whole surface every frame (start with s.clear()):
    // the display double-buffers, so leftovers are two frames old.
    virtual void render(board::gfx::Surface& s) = 0;
};

} // namespace core
