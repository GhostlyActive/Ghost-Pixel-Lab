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

// One frame of touch + button input, sampled once per tick by the app
// manager so apps never talk to the controllers themselves.
struct Input {
    bool    pressed      = false;   // finger is down this frame
    bool    justPressed  = false;   // went down this frame
    bool    justReleased = false;   // went up this frame
    int16_t x = 0, y = 0;           // current position (last one once released)
    int16_t startX = 0, startY = 0; // where the current press began

    // Hardware keys (one-frame events).
    bool backPressed    = false;  // BOOT key; only set if capturesBackButton()
    bool keyPressed     = false;  // PWR key short press (always delivered)
    bool keyLongPressed = false;  // PWR key held ~1.5 s (always delivered)
};

class App {
public:
    virtual ~App() = default;

    virtual const char* name() const = 0;            // shown in the menu
    virtual const char* info() const { return ""; }  // one-line menu subtitle

    virtual void onEnter() {}
    virtual void onExit() {}

    // Claim the BOOT key: it then arrives as Input::backPressed instead of
    // returning to the menu (the top-edge swipe stays as escape hatch).
    virtual bool capturesBackButton() const { return false; }

    // Claim the PWR key: suppresses the system brightness cycle on short
    // press. Its events arrive in Input either way.
    virtual bool capturesPowerKey() const { return false; }

    // dt is seconds since the previous frame, clamped; 0 after a stall.
    virtual void update(const Input& in, float dt) = 0;

    // Must redraw the whole surface every frame (start with s.clear()):
    // the display double-buffers, so leftovers are two frames old.
    virtual void render(board::gfx::Surface& s) = 0;
};

} // namespace core
