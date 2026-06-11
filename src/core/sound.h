// Sound engine: a background mixer task on top of board::audio (16 kHz
// mono). Fire-and-forget tones, PCM clips and MP3 music (any sample rate,
// mono or stereo — resampled on the fly) are mixed together; nothing here
// blocks the app loop.
//
// Rule: an app uses EITHER this engine OR raw board::audio::play() streaming
// (like Piano/Echo) — never both at once. The mixer task sleeps whenever
// nothing plays, so raw streaming stays safe; the app manager additionally
// calls stopAll() on every app switch.
#pragma once

#include <FS.h>
#include <cstddef>
#include <cstdint>

namespace core::sound {

// Starts the codec and the mixer task (idempotent). The play* calls do this
// themselves; call it early if you want the cost at app start.
bool begin();

// Codec output volume, 0..100 (perceptual curve, 100 = 0 dB).
void setMasterVolume(uint8_t percent);

// Simple beep, non-blocking. volume 0..100.
void tone(float freqHz, uint16_t durationMs, uint8_t volume = 70);

// PCM clip, 16 kHz mono int16. The buffer must stay valid while playing
// (flash const arrays or PSRAM). Returns a voice id for stopClip(), or -1.
int  playClip(const int16_t* samples, size_t count, uint8_t volume = 100,
              bool loop = false);
void stopClip(int voice);

// MP3 from memory (buffer must stay valid; e.g. a flash asset) or from a
// file (loaded into PSRAM and freed automatically). One music slot.
bool playMp3(const uint8_t* data, size_t bytes, uint8_t volume = 100,
             bool loop = false);
bool playMp3File(fs::FS& fs, const char* path, uint8_t volume = 100,
                 bool loop = false);
void stopMp3();
[[nodiscard]] bool mp3Playing();

// Stop everything and park the mixer (returns once it is parked).
void stopAll();

} // namespace core::sound
