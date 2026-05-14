#include "imu.h"
#include "i2c.h"
#include "pins.h"

namespace board::imu {

namespace {

constexpr uint8_t REG_WHO_AM_I  = 0x00;     // returns 0x05
constexpr uint8_t REG_CTRL1     = 0x02;
constexpr uint8_t REG_CTRL2     = 0x03;
constexpr uint8_t REG_CTRL3     = 0x04;
constexpr uint8_t REG_CTRL5     = 0x06;
constexpr uint8_t REG_CTRL7     = 0x08;
constexpr uint8_t REG_TEMP_L    = 0x33;
constexpr uint8_t REG_ACCEL_X_L = 0x35;
constexpr uint8_t REG_GYRO_X_L  = 0x3B;

constexpr float ACCEL_LSB_PER_G  = 32768.0f / 4.0f;     // matches CTRL2 = +/-4g
constexpr float GYRO_LSB_PER_DPS = 32768.0f / 512.0f;   // matches CTRL3 = +/-512 dps

bool readPair(uint8_t reg, Vec3& out, float lsb_to_unit) {
    uint8_t b[6];
    if (!i2c::readRegs(pins::QMI8658_ADDR, reg, b, 6)) return false;
    const int16_t rx = static_cast<int16_t>((b[1] << 8) | b[0]);
    const int16_t ry = static_cast<int16_t>((b[3] << 8) | b[2]);
    const int16_t rz = static_cast<int16_t>((b[5] << 8) | b[4]);
    out.x = rx / lsb_to_unit;
    out.y = ry / lsb_to_unit;
    out.z = rz / lsb_to_unit;
    return true;
}

} // namespace

bool begin() {
    uint8_t who = 0;
    if (!i2c::readReg(pins::QMI8658_ADDR, REG_WHO_AM_I, who) || who != 0x05) return false;
    if (!i2c::writeReg(pins::QMI8658_ADDR, REG_CTRL1, 0x60)) return false;   // auto-incr, little endian
    if (!i2c::writeReg(pins::QMI8658_ADDR, REG_CTRL2, 0x23)) return false;   // accel +/-4g, 1000 Hz
    if (!i2c::writeReg(pins::QMI8658_ADDR, REG_CTRL3, 0x53)) return false;   // gyro +/-512 dps, 896 Hz
    if (!i2c::writeReg(pins::QMI8658_ADDR, REG_CTRL5, 0x11)) return false;   // LPF enabled
    if (!i2c::writeReg(pins::QMI8658_ADDR, REG_CTRL7, 0x03)) return false;   // enable accel + gyro
    return true;
}

bool readAccel(Vec3& g)   { return readPair(REG_ACCEL_X_L, g, ACCEL_LSB_PER_G); }
bool readGyro(Vec3& dps)  { return readPair(REG_GYRO_X_L,  dps, GYRO_LSB_PER_DPS); }

float readTemperatureC() {
    uint8_t b[2];
    if (!i2c::readRegs(pins::QMI8658_ADDR, REG_TEMP_L, b, 2)) return 0.0f;
    const int16_t raw = static_cast<int16_t>((b[1] << 8) | b[0]);
    return raw / 256.0f;
}

} // namespace board::imu
