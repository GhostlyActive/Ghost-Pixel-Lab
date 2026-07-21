#include "disk.h"
#include "board/display.h"
#include "core/files.h"
#include "core/hw.h"

#include <cstdio>

namespace apps {

namespace {

using board::gfx::Surface;

constexpr int MARGIN = 14;
constexpr int BTN_W  = 200, BTN_H = 56;
constexpr int BTN_Y  = 330;

constexpr uint16_t COL_BG     = 0x0000;
constexpr uint16_t COL_ROW    = 0x10A2;
constexpr uint16_t COL_ACCENT = 0x07FF;
constexpr uint16_t COL_TEXT   = 0xFFFF;
constexpr uint16_t COL_DIM    = 0x8410;
constexpr uint16_t COL_OK     = 0x07E0;
constexpr uint16_t COL_DANGER = 0xF800;

constexpr float ARM_TIMEOUT = 5.0f;

int btnX() { return (board::display::WIDTH - BTN_W) / 2; }

} // namespace

void Disk::onEnter() {
    armed_ = false;
    done_  = false;
    armT_  = 0;
    refreshT_ = 0;
    refresh();
}

void Disk::refresh() {
    total_ = core::files::totalBytes();
    used_  = core::files::usedBytes();

    programs_ = 0;
    if (fs::FS* fs = core::files::fs()) {
        File dir = fs->open(core::files::basicDir());
        if (dir && dir.isDirectory()) {
            for (File e = dir.openNextFile(); e; e = dir.openNextFile()) {
                if (!e.isDirectory()) ++programs_;
                e.close();
            }
            dir.close();
        }
    }
}

void Disk::update(const core::Input& in, float dt) {
    refreshT_ += dt;
    if (refreshT_ > 2.0f) { refreshT_ = 0; refresh(); }

    if (armed_) {
        armT_ += dt;
        if (armT_ > ARM_TIMEOUT) { armed_ = false; armT_ = 0; }
    }

    const int bx = btnX();
    const bool inBtn = in.x >= bx && in.x < bx + BTN_W &&
                       in.y >= BTN_Y && in.y < BTN_Y + BTN_H;

    if (in.justPressed) pressedBtn_ = inBtn;
    else if (in.justReleased) {
        if (pressedBtn_ && inBtn && core::files::ready()) {
            if (!armed_) { armed_ = true; armT_ = 0; }     // first tap: arm
            else {                                          // second tap: do it
                core::files::wipe();
                armed_ = false;
                done_  = true;
                refresh();
            }
        }
        pressedBtn_ = false;
    }
}

void Disk::render(Surface& s) {
    const int W = board::display::WIDTH;
    char line[48];

    s.clear(COL_BG);
    s.text((W - s.textWidth("DISK", 3)) / 2, 16, "DISK", COL_ACCENT, 3);

    int y = 62;
    std::snprintf(line, sizeof line, "device: %s",
                  core::files::ready() ? (core::files::usingSD() ? "SD CARD" : "internal flash")
                                       : "none");
    s.text(MARGIN, y, line, core::files::usingSD() ? COL_OK : COL_TEXT, 2); y += 30;

    if (total_) {
        std::snprintf(line, sizeof line, "%llu MB total", (unsigned long long)(total_ >> 20));
        s.text(MARGIN, y, line, COL_DIM, 2); y += 24;
        std::snprintf(line, sizeof line, "%llu MB used", (unsigned long long)(used_ >> 20));
        s.text(MARGIN, y, line, COL_DIM, 2); y += 30;
    } else {
        y += 10;
    }

    std::snprintf(line, sizeof line, "%d BASIC program%s", programs_, programs_ == 1 ? "" : "s");
    s.text(MARGIN, y, line, COL_TEXT, 2); y += 34;

    s.text(MARGIN, y, "layout:", COL_DIM, 1); y += 16;
    s.text(MARGIN + 8, y, "/GHOST/BASIC   programs (SAVE/LOAD)", COL_DIM, 1); y += 14;
    s.text(MARGIN + 8, y, "/GHOST/DATA    app data", COL_DIM, 1); y += 14;
    s.text(MARGIN + 8, y, "/GHOST/SYS     settings, paired keyboard", COL_DIM, 1); y += 14;
    s.text(MARGIN + 8, y, "/GHOST/APPS    one folder per app", COL_DIM, 1);

    // The destructive button.
    const int bx = btnX();
    const uint16_t bg = armed_ ? COL_DANGER : COL_ROW;
    s.fillRect(bx, BTN_Y, BTN_W, BTN_H, bg);
    const char* label = armed_ ? "TAP AGAIN TO ERASE" : "FORMAT";
    s.text(bx + (BTN_W - s.textWidth(label, armed_ ? 1 : 2)) / 2,
           BTN_Y + (armed_ ? 24 : 18), label, COL_TEXT, armed_ ? 1 : 2);

    if (armed_) {
        const char* w = "this deletes EVERYTHING on the card";
        s.text((W - s.textWidth(w, 1)) / 2, BTN_Y - 20, w, COL_DANGER, 1);
    } else if (done_) {
        const char* w = "formatted - /GHOST layout recreated";
        s.text((W - s.textWidth(w, 1)) / 2, BTN_Y - 20, w, COL_OK, 1);
    } else {
        const char* w = "erases all files, then rebuilds /GHOST";
        s.text((W - s.textWidth(w, 1)) / 2, BTN_Y - 20, w, COL_DIM, 1);
    }
}

} // namespace apps
