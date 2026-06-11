#include "music.h"
#include "core/sound.h"
#include "board/display.h"
#include "board/storage.h"

#include <Arduino.h>
#include <cstdio>
#include <cstring>

namespace apps {

namespace {

using board::gfx::Surface;

constexpr int MARGIN   = 14;
constexpr int LIST_TOP = 132;
constexpr int ROW_H    = 46;
constexpr int TAP_SLOP = 14;

constexpr uint16_t COL_ACCENT = 0x07FF;
constexpr uint16_t COL_TEXT   = 0xFFFF;
constexpr uint16_t COL_DIM    = 0x8410;
constexpr uint16_t COL_ROW    = 0x10A2;
constexpr uint16_t COL_PLAY   = 0x07E0;

int listHeight() { return board::display::HEIGHT - LIST_TOP - 10; }

bool isMp3(const char* name) {
    const size_t n = strlen(name);
    return n > 4 && strcasecmp(name + n - 4, ".mp3") == 0;
}

} // namespace

void Music::scanDir(uint8_t src) {
    fs::FS* f = (src == 0) ? board::storage::flash() : board::storage::sd();
    if (!f) return;
    File root = f->open("/");
    if (!root || !root.isDirectory()) return;
    for (File e = root.openNextFile(); e && count_ < MAX_FILES;
         e = root.openNextFile()) {
        if (e.isDirectory() || !isMp3(e.name())) continue;
        Entry& it = files_[count_];
        snprintf(it.path, sizeof(it.path), "%s", e.path());
        snprintf(it.label, sizeof(it.label), "%s", e.name());
        it.src = src;
        ++count_;
    }
}

void Music::onEnter() {
    soundOk_    = core::sound::begin();
    playingIdx_ = -1;
    count_      = 0;
    scrollY_    = 0;
    pressIdx_   = -1;
    tracking_   = false;
    scanDir(0);
    scanDir(1);
}

void Music::tapRow(int idx) {
    if (idx < 0 || idx >= count_) return;
    if (idx == playingIdx_) {
        core::sound::stopMp3();
        playingIdx_ = -1;
        return;
    }
    const Entry& e = files_[idx];
    fs::FS* f = (e.src == 0) ? board::storage::flash() : board::storage::sd();
    if (f && core::sound::playMp3File(*f, e.path)) {
        playingIdx_ = idx;
    }
}

void Music::update(const core::Input& in, float) {
    if (playingIdx_ >= 0 && !core::sound::mp3Playing()) playingIdx_ = -1;

    // Volume buttons (top row, big touch targets).
    if (in.justPressed && in.y >= 64 && in.y < 116) {
        if (in.x < board::display::WIDTH / 2 - 40) {
            volume_ = volume_ >= 10 ? volume_ - 10 : 0;
            core::sound::setMasterVolume(volume_);
        } else if (in.x > board::display::WIDTH / 2 + 40) {
            volume_ = volume_ <= 90 ? volume_ + 10 : 100;
            core::sound::setMasterVolume(volume_);
        }
    }

    // File list: drag to scroll, tap to play (same pattern as the menu).
    const int maxScroll =
        count_ * ROW_H > listHeight() ? count_ * ROW_H - listHeight() : 0;
    if (in.justPressed && in.y >= LIST_TOP) {
        tracking_ = true;
        moved_    = 0;
        lastY_    = in.y;
        const int idx = (in.y - LIST_TOP + int(scrollY_)) / ROW_H;
        pressIdx_ = (idx >= 0 && idx < count_) ? idx : -1;
    } else if (in.pressed && tracking_) {
        const int dy = in.y - lastY_;
        lastY_  = in.y;
        moved_ += abs(dy);
        if (moved_ > TAP_SLOP) pressIdx_ = -1;
        scrollY_ -= float(dy);
        if (scrollY_ < 0) scrollY_ = 0;
        if (scrollY_ > maxScroll) scrollY_ = float(maxScroll);
    } else if (in.justReleased && tracking_) {
        tracking_ = false;
        if (pressIdx_ >= 0) tapRow(pressIdx_);
        pressIdx_ = -1;
    }
}

void Music::render(Surface& s) {
    const int W = board::display::WIDTH;
    char line[40];

    s.clear(0x0000);
    s.text((W - s.textWidth("MUSIC", 3)) / 2, 14, "MUSIC", COL_ACCENT, 3);

    // Volume row.
    s.fillRect(MARGIN, 68, 56, 44, COL_ROW);
    s.text(MARGIN + 22, 80, "-", COL_TEXT, 3);
    s.fillRect(W - MARGIN - 56, 68, 56, 44, COL_ROW);
    s.text(W - MARGIN - 38, 80, "+", COL_TEXT, 3);
    snprintf(line, sizeof(line), "VOL %d%%", volume_);
    s.text((W - s.textWidth(line, 2)) / 2, 82, line, COL_TEXT, 2);

    if (!soundOk_) {
        s.text(MARGIN, LIST_TOP, "audio init failed", 0xF800, 2);
        return;
    }
    if (count_ == 0) {
        s.text(MARGIN, LIST_TOP +  8, "no .mp3 files found", COL_TEXT, 2);
        s.text(MARGIN, LIST_TOP + 40, "SD card: copy *.mp3 to the root", COL_DIM, 1);
        s.text(MARGIN, LIST_TOP + 56, "flash:   put files in data/ and run", COL_DIM, 1);
        s.text(MARGIN, LIST_TOP + 72, "         pio run -t uploadfs", COL_DIM, 1);
        return;
    }

    for (int i = 0; i < count_; ++i) {
        const int y = LIST_TOP + i * ROW_H - int(scrollY_);
        if (y + ROW_H < LIST_TOP || y > board::display::HEIGHT) continue;
        const bool playing = (i == playingIdx_);
        const uint16_t bg  = (i == pressIdx_) ? 0x2945 : COL_ROW;
        s.fillRect(MARGIN, y, W - 2 * MARGIN, ROW_H - 6, bg);
        s.text(MARGIN + 34, y + 12, files_[i].label,
               playing ? COL_PLAY : COL_TEXT, 2);
        s.text(MARGIN + 10, y + 12, playing ? ">" : " ", COL_PLAY, 2);
        s.text(W - MARGIN - 22, y + 14, files_[i].src == 0 ? "F" : "S", COL_DIM, 1);
    }

    // Header band stays clean while the list scrolls under it.
    s.fillRect(0, LIST_TOP - 8, W, 2, 0x2945);
}

} // namespace apps
