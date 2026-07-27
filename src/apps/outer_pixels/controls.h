// What the ship is being told to do this frame, with the input device already
// forgotten. The app shell folds an Xbox pad, touch drags and typed keys into
// this one struct, which is why the flight model can be flown by a test with
// no hardware in sight.
#pragma once

namespace apps::outer {

struct Controls {
    float yaw    = 0;   // -1..1, right positive
    float pitch  = 0;   // -1..1, down positive (screen sense)
    float roll   = 0;   // -1..1
    float thrust = 0;   // -1..1, negative brakes

    // Edge-triggered by the shell: true only on the frame the button goes down.
    bool pick   = false;   // A: target the body under the crosshair, or launch
    bool orbits = false;   // Y: toggle the orbit lines
    bool leave  = false;   // B: climb out of surface mode
};

} // namespace apps::outer
