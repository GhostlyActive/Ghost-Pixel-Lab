#include "ble_scan.h"
#include "board/display.h"
#include "core/hw.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace apps {

namespace {

using board::gfx::Surface;
using core::keyboard::PairState;

constexpr int MARGIN   = 12;
constexpr int LIST_TOP = 118;
constexpr int ROW_H    = 52;
constexpr int TAP_SLOP = 14;

// Filter button, top-right of the count row.
constexpr int BTN_W = 104, BTN_H = 26, BTN_Y = 84;
// Forget button, shown only while something is paired.
constexpr int FGT_W = 84, FGT_H = 24, FGT_Y = 38;
// Rescan button, left of the filter toggle.
constexpr int RSC_W = 78;

constexpr uint16_t COL_BG     = 0x0000;
constexpr uint16_t COL_ROW    = 0x10A2;
constexpr uint16_t COL_ROW_HI = 0x2945;
constexpr uint16_t COL_ACCENT = 0x07FF;
constexpr uint16_t COL_TEXT   = 0xFFFF;
constexpr uint16_t COL_DIM    = 0x8410;
constexpr uint16_t COL_OK     = 0x07E0;
constexpr uint16_t COL_WARN   = 0xFD20;

} // namespace

void BleScan::onEnter() {
    core::keyboard::begin();
    core::keyboard::startScan();
    count_     = 0;
    refresh_   = 0;
    scrollY_   = 0;
    pairedIdx_ = -1;
    tracking_  = false;
}

void BleScan::onExit() {
    core::keyboard::stopScan();
}

int BleScan::rowAt(int y) const {
    const int idx = (y - LIST_TOP + int(scrollY_)) / ROW_H;
    return (idx >= 0 && idx < count_) ? idx : -1;
}

void BleScan::update(const core::Input& in, float dt) {
    // Rebuild the list twice a second. Keyboards float to the top; everything
    // else is ordered by signal, so the nearest device is always near the top.
    refresh_ += dt;
    if (refresh_ > 0.5f) {
        refresh_ = 0;
        const int n = core::keyboard::deviceCount();
        count_ = 0;
        for (int i = 0; i < n && count_ < MAX_ROWS; ++i) {
            core::keyboard::Device d = core::keyboard::device(i);
            if (onlyConnectable_ && !d.connectable) continue;   // drop beacons/trackers
            rows_[count_++] = d;
        }
        std::stable_sort(rows_, rows_ + count_,
                         [](const core::keyboard::Device& a, const core::keyboard::Device& b) {
                             const int ka = a.hid ? 2 : (a.appearance == 0x03C1 ? 1 : 0);
                             const int kb = b.hid ? 2 : (b.appearance == 0x03C1 ? 1 : 0);
                             if (ka != kb) return ka > kb;
                             return a.rssi > b.rssi;
                         });
    }

    const int W = board::display::WIDTH;
    const int listH = board::display::HEIGHT - LIST_TOP - 8;
    const int maxScroll = std::max(0, count_ * ROW_H - listH);

    if (in.justPressed) {
        tracking_ = true;
        moved_    = 0;
        lastY_    = in.y;
        const int rscX = W - MARGIN - BTN_W - 8 - RSC_W;
        onButton_ = (in.y >= BTN_Y && in.y < BTN_Y + BTN_H && in.x >= W - MARGIN - BTN_W);
        onRescan_ = (in.y >= BTN_Y && in.y < BTN_Y + BTN_H &&
                     in.x >= rscX && in.x < rscX + RSC_W);
        onForget_ = core::keyboard::savedName()[0] &&
                    in.y >= FGT_Y && in.y < FGT_Y + FGT_H && in.x >= W - MARGIN - FGT_W;
        pressIdx_ = (!onButton_ && !onForget_ && in.y >= LIST_TOP) ? rowAt(in.y) : -1;
    } else if (in.pressed && tracking_) {
        const int dy = in.y - lastY_;
        lastY_  = in.y;
        moved_ += std::abs(dy);
        if (moved_ > TAP_SLOP) { pressIdx_ = -1; onButton_ = false; onForget_ = false; onRescan_ = false; }
        scrollY_ -= float(dy);
        scrollY_ = std::max(0.0f, std::min(scrollY_, float(maxScroll)));
    } else if (in.justReleased && tracking_) {
        tracking_ = false;
        if (onRescan_) {
            core::keyboard::startScan();   // clear the list and listen again
            scrollY_ = 0;
            refresh_ = 1.0f;
        } else if (onForget_) {
            core::keyboard::forget();     // drop the stored pairing, keep scanning
            pairedIdx_ = -1;
            core::keyboard::startScan();
        } else if (onButton_) {
            onlyConnectable_ = !onlyConnectable_;
            refresh_ = 1.0f;          // rebuild on the next frame
            scrollY_ = 0;
        } else if (pressIdx_ >= 0 && pressIdx_ < count_) {
            // Map back to the driver's (unsorted, unfiltered) index.
            const int n = core::keyboard::deviceCount();
            for (int i = 0; i < n; ++i) {
                if (std::strcmp(core::keyboard::device(i).addr, rows_[pressIdx_].addr) == 0) {
                    core::keyboard::pairWith(i);
                    pairedIdx_ = pressIdx_;
                    break;
                }
            }
        }
        pressIdx_ = -1;
        onButton_ = false;
        onForget_ = false;
        onRescan_ = false;
    }
}

void BleScan::render(Surface& s) {
    const int W = board::display::WIDTH;
    const int H = board::display::HEIGHT;
    char line[52];

    s.clear(COL_BG);
    s.text((W - s.textWidth("BLE SCAN", 3)) / 2, 10, "BLE SCAN", COL_ACCENT, 3);

    // What is going on right now: probing, paired, or a hint.
    const PairState ps = core::keyboard::pairState();
    const char* saved  = core::keyboard::savedName();
    if (ps == PairState::Connecting) {
        s.text(MARGIN, 46, "connecting - checking if it is a keyboard...", COL_WARN, 1);
    } else if (ps == PairState::NotAKeyboard) {
        s.text(MARGIN, 46, "that one is not a keyboard - try the next", COL_WARN, 1);
    } else if (saved[0]) {
        std::snprintf(line, sizeof line, "%s %s",
                      core::keyboard::connected() ? "CONNECTED:" : "PAIRED:", saved);
        s.text(MARGIN, 46, line, core::keyboard::connected() ? COL_OK : COL_TEXT, 1);
    } else {
        s.text(MARGIN, 46, "hold the keyboard against the screen,", COL_DIM, 1);
        s.text(MARGIN, 60, "then tap the strongest entry (top of list)", COL_DIM, 1);
    }

    if (saved[0]) {
        const int fx = W - MARGIN - FGT_W;
        s.fillRect(fx, FGT_Y, FGT_W, FGT_H, COL_ROW_HI);
        s.text(fx + 12, FGT_Y + 8, "FORGET", COL_WARN, 1);
    }

    // Count + filter toggle.
    std::snprintf(line, sizeof line, "%d shown / %d seen%s", count_,
                  core::keyboard::seenCount(),
                  core::keyboard::scanning() ? "  scanning" : "");
    s.text(MARGIN, BTN_Y + 8, line, COL_DIM, 1);

    const int rx = W - MARGIN - BTN_W - 8 - RSC_W;
    s.fillRect(rx, BTN_Y, RSC_W, BTN_H, COL_ROW_HI);
    s.text(rx + 12, BTN_Y + 9, "RESCAN", COL_ACCENT, 1);

    const int bx = W - MARGIN - BTN_W;
    s.fillRect(bx, BTN_Y, BTN_W, BTN_H, COL_ROW_HI);
    s.text(bx + 8, BTN_Y + 9, onlyConnectable_ ? "PAIRABLE" : "SHOW ALL", COL_ACCENT, 1);

    s.hLine(MARGIN, LIST_TOP - 10, W - 2 * MARGIN, COL_ROW_HI);

    for (int i = 0; i < count_; ++i) {
        const int y = LIST_TOP + i * ROW_H - int(scrollY_);
        if (y + ROW_H < LIST_TOP || y > H) continue;

        const core::keyboard::Device& d = rows_[i];
        const bool hit = (i == pressIdx_);
        s.fillRect(MARGIN, y, W - 2 * MARGIN, ROW_H - 6, hit ? COL_ROW_HI : COL_ROW);

        s.text(MARGIN + 8, y + 5, d.name[0] ? d.name : "<no name>",
               d.name[0] ? COL_TEXT : COL_DIM, 2);

        // Signal bar: -90..-40 dBm, so you can find it by moving the keyboard.
        int bars = (d.rssi + 90) / 12;
        bars = std::max(0, std::min(4, bars));
        for (int b = 0; b < 4; ++b)
            s.fillRect(W - MARGIN - 46 + b * 9, y + 16 - b * 3, 6, 6 + b * 3,
                       b < bars ? COL_ACCENT : COL_ROW_HI);

        std::snprintf(line, sizeof line, "%s %ddBm", d.addr, d.rssi);
        s.text(MARGIN + 8, y + 30, line, COL_DIM, 1);

        int tagX = W - MARGIN - 46;
        if (d.appearance == 0x03C1) { s.text(tagX, y + 32, "KBD", COL_OK, 1); tagX -= 30; }
        if (d.hid)                    s.text(tagX, y + 32, "HID", COL_ACCENT, 1);
        if (i == pairedIdx_ && ps == PairState::Paired)
            s.text(MARGIN + 8, y + 42, "PAIRED", COL_OK, 1);
    }

    if (count_ == 0) {
        s.text((W - s.textWidth("searching...", 2)) / 2, LIST_TOP + 30,
               "searching...", COL_DIM, 2);
    }
}

} // namespace apps
