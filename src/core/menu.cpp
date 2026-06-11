#include "menu.h"
#include "app_manager.h"
#include "hw.h"

#include "board/display.h"
#include "board/power.h"
#include "board/rtc.h"

#include <Arduino.h>
#include <cstdio>
#include <cstdlib>

namespace core::menu {

namespace {

using board::gfx::Surface;

// Layout.
constexpr int MARGIN   = 14;
constexpr int LIST_TOP = 104;
constexpr int ROW_H    = 86;   // row pitch; the box itself is ROW_H - ROW_PAD
constexpr int ROW_PAD  = 10;
constexpr int STATUS_H = 34;
constexpr int TAP_SLOP = 14;   // more movement than this is a drag, not a tap

// Palette (RGB565).
constexpr uint16_t COL_BG     = 0x0000;
constexpr uint16_t COL_ROW    = 0x10A2;
constexpr uint16_t COL_ROW_HI = 0x2945;
constexpr uint16_t COL_ACCENT = 0x07FF;
constexpr uint16_t COL_TEXT   = 0xFFFF;
constexpr uint16_t COL_DIM    = 0x8410;

class MenuApp final : public App {
public:
    const char* name() const override { return "Menu"; }

    void onEnter() override {
        pressIdx_ = -1;
        tracking_ = false;
        statusMs_ = 0;  // force a status refresh on the first frame
    }

    void update(const Input& in, float) override {
        refreshStatus();
        handleTouch(in);
    }

    void render(Surface& s) override;

private:
    int listHeight() const {
        return board::display::HEIGHT - LIST_TOP - STATUS_H - 6;
    }

    int maxScroll() const {
        const int overflow = manager::count() * ROW_H - listHeight();
        return overflow > 0 ? overflow : 0;
    }

    int rowAt(int y) const {
        const int idx = (y - LIST_TOP + static_cast<int>(scrollY_)) / ROW_H;
        return (idx >= 0 && idx < manager::count()) ? idx : -1;
    }

    // tracking_ guards against presses that began outside the menu (e.g. the
    // home swipe that brought us here): those must not scroll or tap.
    void handleTouch(const Input& in) {
        if (in.justPressed) {
            tracking_ = true;
            moved_    = 0;
            lastY_    = in.y;
            pressIdx_ = (in.y >= LIST_TOP && in.y < LIST_TOP + listHeight())
                            ? rowAt(in.y) : -1;
        } else if (in.pressed && tracking_) {
            const int dy = in.y - lastY_;
            lastY_  = in.y;
            moved_ += std::abs(dy);
            if (moved_ > TAP_SLOP) pressIdx_ = -1;
            scrollY_ -= static_cast<float>(dy);
            if (scrollY_ < 0) scrollY_ = 0;
            if (scrollY_ > maxScroll()) scrollY_ = static_cast<float>(maxScroll());
        } else if (in.justReleased && tracking_) {
            tracking_ = false;
            if (pressIdx_ >= 0) {
                const int idx = pressIdx_;
                pressIdx_ = -1;
                manager::launch(manager::at(idx));
            }
        }
    }

    void refreshStatus() {
        const uint32_t now = millis();
        if (statusMs_ != 0 && now - statusMs_ < 500) return;
        statusMs_ = now;
        battPct_  = hw::power ? board::power::batteryPercent() : -1;
        battV_    = hw::power ? board::power::batteryVolts() : 0.0f;
        charging_ = hw::power && board::power::charging();
        timeOk_   = hw::rtc && board::rtc::read(time_);
    }

    float   scrollY_  = 0;
    int     pressIdx_ = -1;
    int     moved_    = 0;
    int16_t lastY_    = 0;
    bool    tracking_ = false;

    uint32_t statusMs_ = 0;
    int      battPct_  = -1;
    float    battV_    = 0;
    bool     charging_ = false;
    bool     timeOk_   = false;
    board::rtc::DateTime time_{};
};

void MenuApp::render(Surface& s) {
    const int W = board::display::WIDTH;
    const int H = board::display::HEIGHT;

    s.clear(COL_BG);

    // App list first; header and status bar paint over the overflow.
    for (int i = 0; i < manager::count(); ++i) {
        const int y = LIST_TOP + i * ROW_H - static_cast<int>(scrollY_);
        if (y + ROW_H < LIST_TOP || y > H - STATUS_H) continue;
        const uint16_t bg = (i == pressIdx_) ? COL_ROW_HI : COL_ROW;
        s.fillRect(MARGIN, y, W - 2 * MARGIN, ROW_H - ROW_PAD, bg);
        s.fillRect(MARGIN, y, 5, ROW_H - ROW_PAD, COL_ACCENT);
        s.text(MARGIN + 20, y + 14, manager::at(i).name(), COL_TEXT, 3);
        s.text(MARGIN + 20, y + 46, manager::at(i).info(), COL_DIM, 2);
        s.text(W - MARGIN - 26, y + 26, ">", COL_ACCENT, 3);
    }

    // Header. The background mask is only needed when rows can scroll
    // underneath it.
    if (maxScroll() > 0) s.fillRect(0, 0, W, LIST_TOP - 6, COL_BG);
    const char* title = "GHOST PIXEL LAB";
    s.text((W - s.textWidth(title, 3)) / 2, 24, title, COL_ACCENT, 3);
    const char* hint = "tap an app  *  BOOT or top-swipe = back";
    s.text((W - s.textWidth(hint, 1)) / 2, 58, hint, COL_DIM, 1);
    s.hLine(MARGIN, 78, W - 2 * MARGIN, COL_ROW_HI);

    // Status bar.
    const int sy = H - STATUS_H;
    s.fillRect(0, sy, W, STATUS_H, COL_ROW);
    char buf[32];
    if (battPct_ >= 0) {
        snprintf(buf, sizeof(buf), "%d%% %.2fV%s",
                 battPct_, battV_, charging_ ? " CHG" : "");
    } else {
        snprintf(buf, sizeof(buf), "no battery");
    }
    s.text(MARGIN, sy + 10, buf, COL_TEXT, 2);
    if (timeOk_) {
        snprintf(buf, sizeof(buf), "%02u:%02u:%02u",
                 time_.hour, time_.minute, time_.second);
        s.text(W - MARGIN - s.textWidth(buf, 2), sy + 10, buf, COL_TEXT, 2);
    }
}

MenuApp s_menu;

} // namespace

App& instance() { return s_menu; }

} // namespace core::menu
