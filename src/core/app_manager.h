// App manager: registry of apps, runs one at a time, owns the frame loop.
//
// tick() is one frame: sample touch, check the global "go home" controls,
// update + render the active app, present the framebuffer, track FPS.
//
// Back to the menu from any app:
//   - swipe down from the top edge of the screen, or
//   - press (and release) the BOOT button.
#pragma once

#include "app.h"

namespace core::manager {

inline constexpr int MAX_APPS = 16;

// Registration; call from setup() before begin().
void add(App& app);
int  count();
App& at(int i);

void begin();           // starts in the menu
void tick();            // one frame; call from loop()

void launch(App& app);  // switch apps (runs onExit/onEnter)
void goHome();          // back to the menu

// Per-phase frame cost in ms, averaged over the FPS window. Also logged to
// serial every 2 s. The panel transfer runs on a background task, so "show"
// is only the time spent waiting for the previous frame's transfer — near
// zero while drawing is the bottleneck.
struct FrameStats {
    float updateMs, drawMs, showMs;
};

[[nodiscard]] float      fps();
[[nodiscard]] FrameStats frameStats();

} // namespace core::manager
