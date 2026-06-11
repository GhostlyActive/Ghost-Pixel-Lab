// Maps the QMI8658 accelerometer (chip body frame) onto screen coordinates:
// +x right, +y down, +z out of the screen. Shared by every tilt-driven app.
//
// The chip's X axis runs along the board's long edge (= screen Y) and its Y
// axis along the short edge (= screen X). If a direction turns out mirrored
// on real hardware, flip the signs HERE — every app picks it up.
#pragma once

#include "core/hw.h"
#include "board/imu.h"

namespace apps::tilt {

// Gravity in screen space, in g units. False without a working IMU.
inline bool read(float& gx, float& gy, float& gz) {
    if (!core::hw::imu) return false;
    board::imu::Vec3 a;
    if (!board::imu::readAccel(a)) return false;
    gx = -a.y;
    gy = a.x;
    gz = a.z;
    return true;
}

inline bool gravity(float& gx, float& gy) {
    float gz;
    return read(gx, gy, gz);
}

} // namespace apps::tilt
