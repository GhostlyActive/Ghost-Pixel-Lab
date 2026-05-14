// QMI8658 6-axis IMU (accelerometer + gyroscope) on I2C 0x6B.
//
// Default configuration set by begin():
//   accel  +/-4 g     ODR 1000 Hz   LPF mode 0
//   gyro   +/-512 dps ODR 896.8 Hz  LPF mode 3
//
// All vectors are in the chip's body frame (X along the long edge, Y in
// plane perpendicular, Z out of the screen).
#pragma once

namespace board::imu {

struct Vec3 { float x, y, z; };

bool begin();

[[nodiscard]] bool  readAccel(Vec3& g);
[[nodiscard]] bool  readGyro(Vec3& dps);
[[nodiscard]] float readTemperatureC();

} // namespace board::imu
