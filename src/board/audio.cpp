#include "audio.h"
#include "i2c.h"
#include "pins.h"
#include "config.h"

#include <Arduino.h>
#include <ESP_I2S.h>
#include <algorithm>
#include <cmath>

namespace board::audio {

namespace {

I2SClass i2s;
bool     ready          = false;
uint8_t  current_volume = config::DEFAULT_VOLUME;

void wreg(uint8_t r, uint8_t v) {
    i2c::writeReg(pins::ES8311_ADDR, r, v);
}

bool initCodec16k() {
    // The chip keeps state across MCU resets — always start from defaults.
    wreg(0x00, 0x1F);
    delay(20);
    wreg(0x00, 0x00);

    wreg(0x45, 0x00);
    wreg(0x01, 0x30);                     // MCLK/BCLK on, MCLK from pad
    // Clock chain for MCLK = 256*fs (ESP_I2S default: 4.096 MHz at 16 kHz):
    // prediv 1, premult x1. The previous value 0x10 (premult x4) assumed a
    // 64*fs MCLK and ran ADC+DAC 4x off the bus clock -> silence both ways.
    wreg(0x02, 0x00);
    wreg(0x03, 0x10); wreg(0x16, 0x24);   // ADC osr
    wreg(0x04, 0x10); wreg(0x05, 0x00);   // DAC osr, dividers
    wreg(0x06, 0x03);                     // BCLK div 4 (= 64*fs)
    wreg(0x07, 0x00); wreg(0x08, 0xFF);   // LRCK divider 256
    wreg(0x09, 0x0C); wreg(0x0A, 0x0C);   // I2S in/out 16-bit
    wreg(0x0B, 0x00); wreg(0x0C, 0x00);
    wreg(0x10, 0x1F); wreg(0x11, 0x7F);

    wreg(0x00, 0x80);                     // power on, slave mode
    delay(20);

    wreg(0x01, 0x3F);                     // + ADC/DAC/analog clocks on
    wreg(0x0D, 0x01);                     // power up analog circuitry
    wreg(0x0E, 0x02);                     // power up DAC/PGA references
    wreg(0x12, 0x00);                     // DAC powered
    wreg(0x13, 0x10);                     // output stage enabled
    wreg(0x14, 0x1A);                     // analog mic + PGA gain
    wreg(0x15, 0x40);
    wreg(0x17, 0xBF);                     // ADC digital volume 0 dB
    wreg(0x1B, 0x0A); wreg(0x1C, 0x6A);
    wreg(0x31, 0x00);                     // DAC unmuted
    wreg(0x37, 0x08); wreg(0x44, 0x08);
    return true;
}

} // namespace

bool begin(uint32_t sampleRate) {
    if (ready) return true;  // I2S/codec already up; rate stays as configured

    // ES8311 chip ID check: tells "codec missing on I2C" apart from a
    // wrong register/I2S configuration when debugging silence.
    uint8_t id1 = 0, id2 = 0;
    if (!i2c::readReg(pins::ES8311_ADDR, 0xFD, id1) ||
        !i2c::readReg(pins::ES8311_ADDR, 0xFE, id2) ||
        id1 != 0x83 || id2 != 0x11) {
        Serial.printf("[audio] ES8311 not found (id=%02X %02X)\n", id1, id2);
        return false;
    }
    Serial.printf("[audio] ES8311 ok (id=%02X %02X)\n", id1, id2);

    pinMode(pins::PA_EN, OUTPUT);
    digitalWrite(pins::PA_EN, HIGH);

    i2s.setPins(pins::I2S_BCK, pins::I2S_WS, pins::I2S_DO, pins::I2S_DI, pins::I2S_MCK);
    // LEFT slot only: the ES8311 is mono and talks on the left channel. With
    // SLOT_BOTH the RX side records both stereo slots (every 2nd sample is
    // junk), which plays back as ring-modulated "retro beeping".
    if (!i2s.begin(I2S_MODE_STD, sampleRate, I2S_DATA_BIT_WIDTH_16BIT,
                   I2S_SLOT_MODE_MONO, I2S_STD_SLOT_LEFT)) {
        Serial.println("[audio] I2S init failed");
        return false;
    }
    Serial.println("[audio] I2S up, codec configured");
    if (sampleRate == 16000 && !initCodec16k()) return false;
    setVolume(current_volume);
    ready = true;
    return true;
}

void setVolume(uint8_t percent) {
    if (percent > 100) percent = 100;
    current_volume = percent;
    // DAC volume register counts in 0.5 dB steps: 0x00 = mute, 0xBF = 0 dB,
    // above that digital gain that clips hard. Map percent onto -33..0 dB,
    // which tracks perceived loudness far better than a linear register
    // mapping (30 % linear would be -67 dB = inaudible).
    const uint8_t v = (percent == 0)
        ? 0
        : static_cast<uint8_t>(0xBF - ((100 - percent) * 66) / 100);
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
