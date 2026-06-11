#include "piano.h"
#include "board/audio.h"
#include "board/display.h"

#include <cmath>
#include <cstdio>

namespace apps {

namespace {

using board::gfx::Surface;

constexpr uint32_t SAMPLE_RATE = 16000;
constexpr size_t   CHUNK_MAX   = 1024;
constexpr float    TWO_PI_F    = 6.28318530718f;
constexpr int16_t  AMPLITUDE   = 11000;

// Keyboard layout (portrait): 8 white keys across the bottom, black keys
// overlapping the upper part of the gaps after C D F G A.
constexpr int KEY_TOP  = 180;
constexpr int WHITE_W  = 46;       // 8 * 46 = 368
constexpr int BLACK_W  = 30;
constexpr int BLACK_H  = 150;

constexpr float WHITE_F[8] = {261.63f, 293.66f, 329.63f, 349.23f,
                              392.00f, 440.00f, 493.88f, 523.25f};
constexpr float BLACK_F[5] = {277.18f, 311.13f, 369.99f, 415.30f, 466.16f};
constexpr int   BLACK_GAP[5] = {0, 1, 3, 4, 5};   // white index left of each
const char* const WHITE_N[8] = {"C4","D4","E4","F4","G4","A4","B4","C5"};
const char* const BLACK_N[5] = {"C#4","D#4","F#4","G#4","A#4"};

constexpr uint16_t COL_ACCENT = 0x07FF;
constexpr uint16_t COL_DIM    = 0x8410;
constexpr uint16_t COL_BLACK  = 0x2104;

// 0..7 white, 8..12 black, -1 none.
int hitTest(int x, int y) {
    if (y < KEY_TOP || y >= board::display::HEIGHT) return -1;
    if (y < KEY_TOP + BLACK_H) {
        for (int k = 0; k < 5; ++k) {
            const int cx = (BLACK_GAP[k] + 1) * WHITE_W;
            if (x >= cx - BLACK_W / 2 && x < cx + BLACK_W / 2) return 8 + k;
        }
    }
    int wi = x / WHITE_W;
    if (wi < 0) wi = 0;
    if (wi > 7) wi = 7;
    return wi;
}

float noteFreq(int n)        { return n >= 8 ? BLACK_F[n - 8] : WHITE_F[n]; }
const char* noteName(int n)  { return n >= 8 ? BLACK_N[n - 8] : WHITE_N[n]; }

} // namespace

void Piano::onEnter() {
    audioOk_ = board::audio::begin(SAMPLE_RATE);
    board::audio::setVolume(30);
    board::audio::setSpeakerEnable(true);
    note_  = -1;
    env_   = 0;
    phase_ = 0;
}

void Piano::onExit() {
    board::audio::setSpeakerEnable(false);
}

void Piano::update(const core::Input& in, float dt) {
    note_ = in.pressed ? hitTest(in.x, in.y) : -1;
    if (note_ >= 0) freq_ = noteFreq(note_);

    if (!audioOk_) return;

    size_t n = static_cast<size_t>(dt * SAMPLE_RATE);
    if (n > CHUNK_MAX) n = CHUNK_MAX;

    const bool held = note_ >= 0;
    if (n == 0 || (!held && env_ < 0.0005f)) return;  // idle: DMA auto-clears

    // Per-sample one-pole envelope: ~5 ms attack, ~25 ms release (no clicks).
    static int16_t buf[CHUNK_MAX];
    const float target = held ? 1.0f : 0.0f;
    const float rate   = held ? 0.012f : 0.0025f;
    const float step   = TWO_PI_F * freq_ / SAMPLE_RATE;
    for (size_t i = 0; i < n; ++i) {
        env_  += (target - env_) * rate;
        buf[i] = static_cast<int16_t>(sinf(phase_) * env_ * AMPLITUDE);
        phase_ += step;
        if (phase_ >= TWO_PI_F) phase_ -= TWO_PI_F;
    }
    board::audio::play(buf, n);
}

void Piano::render(Surface& s) {
    const int W = board::display::WIDTH;
    const int H = board::display::HEIGHT;

    s.clear(0x0000);
    s.text((W - s.textWidth("PIANO", 3)) / 2, 16, "PIANO", COL_ACCENT, 3);

    if (!audioOk_) {
        s.text((W - s.textWidth("audio init failed", 2)) / 2, 90,
               "audio init failed", 0xF800, 2);
    } else if (note_ >= 0) {
        char line[24];
        snprintf(line, sizeof(line), "%s  %.0f Hz", noteName(note_), freq_);
        s.text((W - s.textWidth(line, 3)) / 2, 90, line, 0xFFFF, 3);
    } else {
        s.text((W - s.textWidth("touch a key", 2)) / 2, 96,
               "touch a key", COL_DIM, 2);
    }

    // White keys.
    for (int i = 0; i < 8; ++i) {
        const int x = i * WHITE_W;
        const uint16_t col = (note_ == i) ? COL_ACCENT : 0xFFFF;
        s.fillRect(x + 1, KEY_TOP, WHITE_W - 2, H - KEY_TOP, col);
        s.text(x + (WHITE_W - 12) / 2, H - 26, WHITE_N[i], 0x0000, 2);
    }
    // Black keys on top.
    for (int k = 0; k < 5; ++k) {
        const int cx = (BLACK_GAP[k] + 1) * WHITE_W;
        const uint16_t col = (note_ == 8 + k) ? COL_ACCENT : COL_BLACK;
        s.fillRect(cx - BLACK_W / 2, KEY_TOP, BLACK_W, BLACK_H, col);
    }
}

} // namespace apps
