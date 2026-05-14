// ES8311 mono codec + I2S audio path.
//
// Speaker output: I2S_DO -> codec DAC -> PA -> internal speaker.
// Microphone input: codec ADC -> I2S_DI.
//
// begin() runs a minimal register sequence for 16 kHz / 16-bit / mono PCM.
// For other rates the full coefficient table from Espressif's es8311 driver
// is needed (see the original Waveshare example).
#pragma once

#include <cstddef>
#include <cstdint>

namespace board::audio {

bool begin(uint32_t sampleRate = 16000);

void setVolume(uint8_t percent);        // 0..100
void setSpeakerEnable(bool on);

size_t play(const int16_t* samples, size_t count);
size_t record(int16_t* buffer, size_t count);

void beep(uint16_t frequencyHz, uint16_t durationMs);

} // namespace board::audio
