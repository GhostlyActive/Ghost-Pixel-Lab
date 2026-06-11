#include "echo.h"
#include "board/audio.h"
#include "board/display.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <cstdio>
#include <cstdlib>

namespace apps {

namespace {

using board::gfx::Surface;

constexpr uint32_t SAMPLE_RATE = 16000;
constexpr size_t   MAX_SAMPLES = SAMPLE_RATE * 8;  // 8 s = 256 KB PSRAM
constexpr size_t   CHUNK_MAX   = 512;              // worst-case ~32 ms block

constexpr int WAVE_Y = 130;   // waveform strip centerline
constexpr int WAVE_H = 70;    // half height
constexpr int BTN_Y  = 330;   // record button center
constexpr int BTN_R  = 75;

constexpr uint16_t COL_ACCENT = 0x07FF;
constexpr uint16_t COL_TEXT   = 0xFFFF;
constexpr uint16_t COL_DIM    = 0x8410;
constexpr uint16_t COL_REC    = 0xF800;
constexpr uint16_t COL_PLAY   = 0x07E0;

} // namespace

void Echo::onEnter() {
    const bool audioOk = board::audio::begin(SAMPLE_RATE);
    board::audio::setSpeakerEnable(false);  // mic only until playback

    if (!buf_) {
        buf_ = static_cast<int16_t*>(heap_caps_malloc(
            MAX_SAMPLES * sizeof(int16_t), MALLOC_CAP_SPIRAM));
    }
    state_ = !audioOk ? State::NoAudio
           : !buf_    ? State::NoMem
                      : State::Idle;
    recLen_  = 0;
    playPos_ = 0;
    peak_    = 0;
}

void Echo::onExit() {
    board::audio::setSpeakerEnable(false);
    free(buf_);
    buf_ = nullptr;
}

void Echo::startRecording() {
    // Drain the RX DMA so the take starts now, not ~90 ms in the past.
    static int16_t scratch[CHUNK_MAX];
    for (int i = 0; i < 3; ++i) board::audio::record(scratch, CHUNK_MAX);
    recLen_ = 0;
    peak_   = 0;
    state_  = State::Recording;
}

void Echo::startPlayback() {
    board::audio::setSpeakerEnable(true);
    playPos_ = 0;
    // Prime the DMA with ~64 ms so frame jitter can't underrun right away.
    const size_t pre = recLen_ < 1024 ? recLen_ : 1024;
    playPos_ += board::audio::play(buf_, pre);
    state_ = State::Playing;
}

void Echo::stopToIdle() {
    board::audio::setSpeakerEnable(false);
    state_ = State::Idle;
}

void Echo::update(const core::Input& in, float dt) {
    // Samples that arrived / drained since the last frame; reading or
    // writing this much keeps pace with the I2S clock without blocking.
    size_t n = static_cast<size_t>(dt * SAMPLE_RATE);
    if (n > CHUNK_MAX) n = CHUNK_MAX;

    switch (state_) {
    case State::NoAudio:
    case State::NoMem:
        break;

    case State::Idle:
        if (in.justPressed && in.y > 70) startRecording();
        break;

    case State::Recording: {
        const size_t space = MAX_SAMPLES - recLen_;
        if (n > space) n = space;
        if (n > 0) {
            const size_t got = board::audio::record(buf_ + recLen_, n);
            int16_t chunkPeak = 0;
            for (size_t i = 0; i < got; ++i) {
                const int16_t v = abs(buf_[recLen_ + i]);
                if (v > chunkPeak) chunkPeak = v;
            }
            recLen_ += got;
            peak_ = max(peak_ * (1.0f - 4.0f * dt), chunkPeak / 32768.0f);
        }
        if (!in.pressed || recLen_ >= MAX_SAMPLES) {
            if (recLen_ > SAMPLE_RATE / 10) startPlayback();
            else                            state_ = State::Idle;
        }
        break;
    }

    case State::Playing: {
        if (in.justPressed && in.y > 70) {  // interrupt: new take
            stopToIdle();
            startRecording();
            break;
        }
        const size_t left = recLen_ - playPos_;
        if (n > left) n = left;
        if (n > 0) playPos_ += board::audio::play(buf_ + playPos_, n);
        if (playPos_ >= recLen_) stopToIdle();
        break;
    }
    }
}

void Echo::render(Surface& s) {
    const int W = board::display::WIDTH;
    char line[48];

    s.clear(0x0000);
    s.text((W - s.textWidth("ECHO", 3)) / 2, 14, "ECHO", COL_ACCENT, 3);

    if (state_ == State::NoAudio || state_ == State::NoMem) {
        const char* msg = state_ == State::NoAudio ? "audio init failed"
                                                   : "PSRAM alloc failed";
        s.text((W - s.textWidth(msg, 2)) / 2, 200, msg, COL_REC, 2);
        return;
    }

    // Waveform of the take so far (or the finished one).
    s.hLine(0, WAVE_Y, W, 0x2945);
    if (recLen_ > 0) {
        for (int x = 0; x < W; ++x) {
            const size_t idx = recLen_ * x / W;
            const int h = abs(buf_[idx]) * WAVE_H / 32768;
            if (h > 0) s.fillRect(x, WAVE_Y - h, 1, 2 * h + 1, COL_ACCENT);
        }
        if (state_ == State::Playing) {
            const int cx = static_cast<int>(playPos_ * W / recLen_);
            s.fillRect(cx, WAVE_Y - WAVE_H, 2, 2 * WAVE_H, COL_TEXT);
        }
        snprintf(line, sizeof(line), "%.1f s", recLen_ / float(SAMPLE_RATE));
        s.text((W - s.textWidth(line, 2)) / 2, WAVE_Y + WAVE_H + 10, line, COL_DIM, 2);
    }

    // Record button.
    const uint16_t btnCol = state_ == State::Recording ? COL_REC
                          : state_ == State::Playing   ? COL_PLAY
                                                       : 0x2945;
    s.filledCircle(W / 2, BTN_Y, BTN_R, btnCol);
    const char* label = state_ == State::Recording ? "REC"
                      : state_ == State::Playing   ? "PLAY"
                                                   : "HOLD";
    s.text(W / 2 - s.textWidth(label, 3) / 2, BTN_Y - 10, label,
           state_ == State::Idle ? COL_DIM : 0x0000, 3);

    if (state_ == State::Idle) {
        const char* hint = "hold to record, release to play";
        s.text((W - s.textWidth(hint, 1)) / 2, BTN_Y + BTN_R + 14, hint, COL_DIM, 1);
    }

    // Mic level while recording.
    if (state_ == State::Recording) {
        const int w = static_cast<int>(peak_ * (W - 28));
        s.fillRect(14, board::display::HEIGHT - 22, W - 28, 10, 0x2945);
        s.fillRect(14, board::display::HEIGHT - 22, w, 10, COL_REC);
    }
}

} // namespace apps
