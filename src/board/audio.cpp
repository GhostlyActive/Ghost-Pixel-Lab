#include "audio.h"
#include "i2c.h"
#include "pins.h"

#include <Arduino.h>
#include <ESP_I2S.h>
#include <algorithm>
#include <cmath>

namespace board::audio {

namespace {

I2SClass i2s;
bool     ready          = false;
uint8_t  current_volume = 80;

void wreg(uint8_t r, uint8_t v) {
    i2c::writeReg(pins::ES8311_ADDR, r, v);
}

bool initCodec16k() {
    wreg(0x45, 0x00);
    wreg(0x01, 0x30);
    wreg(0x02, 0x10);
    wreg(0x03, 0x10); wreg(0x16, 0x24);
    wreg(0x04, 0x10); wreg(0x05, 0x00);
    wreg(0x06, 0x03);
    wreg(0x07, 0x00); wreg(0x08, 0xFF);
    wreg(0x09, 0x0C); wreg(0x0A, 0x0C);
    wreg(0x0B, 0x00); wreg(0x0C, 0x00);
    wreg(0x10, 0x1F); wreg(0x11, 0x7F);
    wreg(0x00, 0x80);
    wreg(0x0D, 0x01);
    wreg(0x12, 0x00);
    wreg(0x13, 0x10);
    wreg(0x14, 0x18);
    wreg(0x15, 0x40);
    wreg(0x1B, 0x0A); wreg(0x1C, 0x6A);
    wreg(0x37, 0x08); wreg(0x44, 0x08);
    return true;
}

} // namespace

bool begin(uint32_t sampleRate) {
    pinMode(pins::PA_EN, OUTPUT);
    digitalWrite(pins::PA_EN, HIGH);

    i2s.setPins(pins::I2S_BCK, pins::I2S_WS, pins::I2S_DO, pins::I2S_DI, pins::I2S_MCK);
    if (!i2s.begin(I2S_MODE_STD, sampleRate, I2S_DATA_BIT_WIDTH_16BIT,
                   I2S_SLOT_MODE_MONO, I2S_STD_SLOT_BOTH)) {
        return false;
    }
    if (sampleRate == 16000 && !initCodec16k()) return false;
    setVolume(current_volume);
    ready = true;
    return true;
}

void setVolume(uint8_t percent) {
    if (percent > 100) percent = 100;
    current_volume = percent;
    const uint8_t v = static_cast<uint16_t>(percent) * 255 / 100;
    i2c::writeReg(pins::ES8311_ADDR, 0x32, v);
}

void setSpeakerEnable(bool on) {
    digitalWrite(pins::PA_EN, on ? HIGH : LOW);
}

size_t play(const int16_t* samples, size_t count) {
    if (!ready) return 0;
    return i2s.write(reinterpret_cast<const uint8_t*>(samples), count * 2) / 2;
}

size_t record(int16_t* buffer, size_t count) {
    if (!ready) return 0;
    return i2s.readBytes(reinterpret_cast<char*>(buffer), count * 2) / 2;
}

void beep(uint16_t frequencyHz, uint16_t durationMs) {
    if (!ready) return;
    constexpr uint32_t SAMPLE_RATE = 16000;
    constexpr int16_t  AMPLITUDE   = 12000;
    constexpr float    TWO_PI_F    = 6.28318530718f;
    const size_t total = SAMPLE_RATE * durationMs / 1000;

    int16_t buf[256];
    size_t  emitted = 0;
    while (emitted < total) {
        const size_t n = std::min<size_t>(sizeof(buf) / 2, total - emitted);
        for (size_t i = 0; i < n; ++i) {
            const float t = static_cast<float>(emitted + i) / SAMPLE_RATE;
            buf[i] = static_cast<int16_t>(AMPLITUDE * std::sin(TWO_PI_F * frequencyHz * t));
        }
        play(buf, n);
        emitted += n;
    }
}

} // namespace board::audio
