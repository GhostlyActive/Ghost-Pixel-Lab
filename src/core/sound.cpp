#include "sound.h"
#include "board/audio.h"
#include "board/storage.h"

#include <Arduino.h>
#include <MP3DecoderHelix.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace core::sound {

namespace {

constexpr int   SR       = 16000;
constexpr int   MIX_N    = 256;   // samples per mixer pass (16 ms)
constexpr int   N_VOICES = 4;
constexpr float TWO_PI_F = 6.28318530718f;

struct Voice {
    enum class Type : uint8_t { Off, Tone, Clip };
    Type  type = Type::Off;
    float vol  = 1.0f;
    // tone
    float    phase = 0, step = 0;
    float    env = 0, envTarget = 0;  // short ramp against clicks
    uint32_t left = 0;                // tone samples until release
    // clip
    const int16_t* data = nullptr;
    size_t len = 0, pos = 0;
    bool   loop = false;
};

Voice             s_v[N_VOICES];
SemaphoreHandle_t s_lock = nullptr;
TaskHandle_t      s_task = nullptr;
volatile bool     s_parked = true;

// --- music slot (MP3) ----------------------------------------------------
libhelix::MP3DecoderHelix* s_dec = nullptr;
const uint8_t* s_mp3      = nullptr;
size_t         s_mp3Len   = 0, s_mp3Pos = 0;
bool           s_mp3Loop  = false, s_mp3Done = false;
float          s_mp3Vol   = 1.0f;
uint8_t*       s_mp3Owned = nullptr;  // freed when stopped (playMp3File)

// Ring buffer of decoded PCM, already 16 kHz mono. ~0.5 s.
constexpr size_t RING_N = 8192;
int16_t s_ring[RING_N];
size_t  s_rHead = 0, s_rTail = 0;
float   s_srcPos = 0;  // resampler position carry between frames

size_t ringCount() { return (s_rHead - s_rTail + RING_N) % RING_N; }
size_t ringSpace() { return RING_N - 1 - ringCount(); }

// Helix callback: downmix + nearest-neighbour resample to 16 kHz mono.
void mp3Pcm(MP3FrameInfo& info, short* pcm, size_t len, void*) {
    const int    ch     = info.nChans > 0 ? info.nChans : 1;
    const size_t frames = len / ch;
    const float  step   = info.samprate > 0 ? float(info.samprate) / SR : 1.0f;
    float pos = s_srcPos;
    while (pos < frames && ringSpace() > 0) {
        const size_t i = size_t(pos) * ch;
        int32_t v = pcm[i];
        if (ch > 1) v = (v + pcm[i + 1]) / 2;
        s_ring[s_rHead] = int16_t(v);
        s_rHead = (s_rHead + 1) % RING_N;
        pos += step;
    }
    s_srcPos = pos - float(frames);
}

bool anythingActive() {
    if (s_mp3) return true;
    for (const auto& v : s_v)
        if (v.type != Voice::Type::Off) return true;
    return false;
}

// Lock must be held.
void freeMp3Locked() {
    if (s_dec) s_dec->end();
    s_mp3    = nullptr;
    s_mp3Len = s_mp3Pos = 0;
    s_mp3Done = false;
    free(s_mp3Owned);
    s_mp3Owned = nullptr;
    s_rHead = s_rTail = 0;
    s_srcPos = 0;
}

void mixMusic(int32_t* acc) {
    for (int i = 0; i < MIX_N && ringCount() > 0; ++i) {
        acc[i] += int32_t(s_ring[s_rTail] * s_mp3Vol);
        s_rTail = (s_rTail + 1) % RING_N;
    }
    if (s_mp3Done && ringCount() == 0) freeMp3Locked();
}

void mixVoice(Voice& v, int32_t* acc) {
    if (v.type == Voice::Type::Tone) {
        for (int i = 0; i < MIX_N; ++i) {
            v.env += (v.envTarget - v.env) * 0.015f;
            acc[i] += int32_t(sinf(v.phase) * v.env * v.vol * 11000.0f);
            v.phase += v.step;
            if (v.phase >= TWO_PI_F) v.phase -= TWO_PI_F;
            if (v.left > 0 && --v.left == 0) v.envTarget = 0;
        }
        if (v.envTarget == 0 && v.env < 0.001f) v.type = Voice::Type::Off;
    } else if (v.type == Voice::Type::Clip) {
        for (int i = 0; i < MIX_N; ++i) {
            if (v.pos >= v.len) {
                if (!v.loop) { v.type = Voice::Type::Off; return; }
                v.pos = 0;
            }
            acc[i] += int32_t(v.data[v.pos++] * v.vol);
        }
    }
}

void mixerTask(void*) {
    static int16_t out[MIX_N];
    static int32_t acc[MIX_N];

    for (;;) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        if (!anythingActive()) {
            if (!s_parked) board::audio::setSpeakerEnable(false);
            s_parked = true;
            xSemaphoreGive(s_lock);
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            continue;
        }
        if (s_parked) {
            board::audio::setSpeakerEnable(true);
            s_parked = false;
        }

        // Keep the decoder ahead of the mixer while there is ring space for
        // a worst-case decoded frame.
        while (s_mp3 && !s_mp3Done && ringSpace() > 1400) {
            if (s_mp3Pos >= s_mp3Len) {
                if (s_mp3Loop) s_mp3Pos = 0;
                else { s_mp3Done = true; break; }
            }
            const size_t chunk = std::min<size_t>(512, s_mp3Len - s_mp3Pos);
            const size_t used  = s_dec->write(s_mp3 + s_mp3Pos, chunk);
            if (used == 0) { s_mp3Done = true; break; }  // decoder stuck
            s_mp3Pos += used;
        }

        memset(acc, 0, sizeof(acc));
        if (s_mp3) mixMusic(acc);
        for (auto& v : s_v) mixVoice(v, acc);
        for (int i = 0; i < MIX_N; ++i) {
            int32_t s = acc[i];
            if (s >  32767) s =  32767;
            if (s < -32768) s = -32768;
            out[i] = int16_t(s);
        }
        xSemaphoreGive(s_lock);

        board::audio::play(out, MIX_N);  // blocks on the DMA: paces the loop
    }
}

void wake() {
    if (s_task) xTaskNotifyGive(s_task);
}

bool startMp3Locked(const uint8_t* data, size_t bytes, uint8_t volume,
                    bool loop, uint8_t* owned) {
    freeMp3Locked();
    if (!s_dec) s_dec = new libhelix::MP3DecoderHelix(mp3Pcm);
    if (!s_dec->begin()) { free(owned); return false; }
    s_mp3      = data;
    s_mp3Len   = bytes;
    s_mp3Pos   = 0;
    s_mp3Loop  = loop;
    s_mp3Vol   = volume / 100.0f;
    s_mp3Owned = owned;
    return true;
}

} // namespace

bool begin() {
    if (!board::audio::begin(SR)) return false;
    if (!s_task) {
        board::audio::setSpeakerEnable(false);  // mixer enables on demand
        s_lock = xSemaphoreCreateMutex();
        // Core 0 next to the display presenter; prio above it so audio
        // never stutters. Blocks on I2S DMA, so the idle task still runs.
        if (xTaskCreatePinnedToCore(mixerTask, "sound", 16384, nullptr, 6,
                                    &s_task, 0) != pdPASS) {
            s_task = nullptr;
            return false;
        }
    }
    return true;
}

void setMasterVolume(uint8_t percent) {
    board::audio::setVolume(percent);
}

void tone(float freqHz, uint16_t durationMs, uint8_t volume) {
    if (!begin()) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    Voice* v = nullptr;
    for (auto& c : s_v) if (c.type == Voice::Type::Off) { v = &c; break; }
    if (v) {
        *v = Voice{};
        v->type      = Voice::Type::Tone;
        v->vol       = volume / 100.0f;
        v->step      = TWO_PI_F * freqHz / SR;
        v->left      = uint32_t(SR) * durationMs / 1000;
        v->envTarget = 1.0f;
    }
    xSemaphoreGive(s_lock);
    wake();
}

int playClip(const int16_t* samples, size_t count, uint8_t volume, bool loop) {
    if (!begin() || !samples || count == 0) return -1;
    int id = -1;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    for (int i = 0; i < N_VOICES; ++i) {
        if (s_v[i].type == Voice::Type::Off) {
            s_v[i] = Voice{};
            s_v[i].type = Voice::Type::Clip;
            s_v[i].vol  = volume / 100.0f;
            s_v[i].data = samples;
            s_v[i].len  = count;
            s_v[i].loop = loop;
            id = i;
            break;
        }
    }
    xSemaphoreGive(s_lock);
    wake();
    return id;
}

void stopClip(int voice) {
    if (!s_task || voice < 0 || voice >= N_VOICES) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_v[voice].type == Voice::Type::Clip) s_v[voice].type = Voice::Type::Off;
    xSemaphoreGive(s_lock);
}

bool playMp3(const uint8_t* data, size_t bytes, uint8_t volume, bool loop) {
    if (!begin() || !data || bytes == 0) return false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const bool ok = startMp3Locked(data, bytes, volume, loop, nullptr);
    xSemaphoreGive(s_lock);
    wake();
    return ok;
}

bool playMp3File(fs::FS& fs, const char* path, uint8_t volume, bool loop) {
    if (!begin()) return false;
    size_t   n   = 0;
    uint8_t* buf = board::storage::loadToPsram(fs, path, n);
    if (!buf) return false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const bool ok = startMp3Locked(buf, n, volume, loop, buf);
    xSemaphoreGive(s_lock);
    wake();
    return ok;
}

void stopMp3() {
    if (!s_task) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    freeMp3Locked();
    xSemaphoreGive(s_lock);
}

bool mp3Playing() {
    return s_task && s_mp3 != nullptr;
}

void stopAll() {
    if (!s_task) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    freeMp3Locked();
    for (auto& v : s_v) v.type = Voice::Type::Off;
    xSemaphoreGive(s_lock);
    wake();  // let the task notice and park
    for (int i = 0; i < 50 && !s_parked; ++i) vTaskDelay(pdMS_TO_TICKS(2));
}

} // namespace core::sound
