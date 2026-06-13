#include "pad_lab.h"
#include "board/display.h"

#include <Arduino.h>
#include <cstdio>

namespace apps {

namespace {

using board::gfx::Surface;

constexpr uint16_t COL_ACCENT = 0x07FF;
constexpr uint16_t COL_TEXT   = 0xFFFF;
constexpr uint16_t COL_DIM    = 0x8410;
constexpr uint16_t COL_BASE   = 0x2945;
constexpr uint16_t COL_A      = 0x07E0;  // green
constexpr uint16_t COL_B      = 0xF800;  // red
constexpr uint16_t COL_X      = 0x051F;  // blue
constexpr uint16_t COL_Y      = 0xFFE0;  // yellow

void stick(Surface& s, int cx, int cy, float x, float y, bool click) {
    s.circle(cx, cy, 46, COL_BASE);
    s.circle(cx, cy, 10, COL_BASE);
    const int dx = cx + int(x * 34.0f);
    const int dy = cy + int(y * 34.0f);
    s.filledCircle(dx, dy, 10, click ? COL_ACCENT : COL_TEXT);
}

void faceButton(Surface& s, int cx, int cy, const char* label,
                bool pressed, uint16_t col) {
    if (pressed) s.filledCircle(cx, cy, 16, col);
    else         s.circle(cx, cy, 16, col);
    s.text(cx - 6, cy - 7, label, pressed ? 0x0000 : col, 2);
}

void smallButton(Surface& s, int cx, int cy, const char* label, bool pressed) {
    if (pressed) s.filledCircle(cx, cy, 12, COL_ACCENT);
    else         s.circle(cx, cy, 12, COL_BASE);
    s.text(cx - 6, cy + 18, label, COL_DIM, 1);
}

} // namespace

void PadLab::onEnter() {
    bleOk_ = core::pad::begin();
    prevA_ = false;
}

void PadLab::update(const core::Input&, float) {
    st_ = core::pad::state();
    if (st_.a && !prevA_) core::pad::rumble(60, 0, 300);  // press A = brrr
    prevA_ = st_.a;
}

void PadLab::render(Surface& s) {
    const int W = board::display::WIDTH;
    char line[40];

    s.clear(0x0000);
    s.text((W - s.textWidth("PAD LAB", 3)) / 2, 14, "PAD LAB", COL_ACCENT, 3);

    if (!bleOk_) {
        s.text((W - s.textWidth("BLE init failed", 2)) / 2, 200,
               "BLE init failed", 0xF800, 2);
        return;
    }

    if (!st_.connected) {
        const int dots = (millis() / 400) % 4;
        snprintf(line, sizeof(line), "searching%.*s", dots, "...");
        s.text((W - s.textWidth("searching...", 2)) / 2, 150, line, COL_TEXT, 2);
        s.text((W - s.textWidth("hold the pair button on top", 1)) / 2, 200,
               "hold the pair button on top", COL_DIM, 1);
        s.text((W - s.textWidth("until the Xbox logo blinks fast", 1)) / 2, 216,
               "until the Xbox logo blinks fast", COL_DIM, 1);
        s.text((W - s.textWidth("(1708: firmware via Xbox Accessories app)", 1)) / 2, 248,
               "(1708: firmware via Xbox Accessories app)", COL_DIM, 1);
        return;
    }

    if (st_.battery > 0) {
        snprintf(line, sizeof(line), "BAT %d%%", st_.battery);
        s.text(W - 14 - s.textWidth(line, 1), 8, line, COL_DIM, 1);
    }

    // Bumpers.
    s.fillRect(30, 60, 120, 26, st_.lb ? COL_ACCENT : COL_BASE);
    s.text(78, 66, "LB", st_.lb ? 0x0000 : COL_DIM, 2);
    s.fillRect(218, 60, 120, 26, st_.rb ? COL_ACCENT : COL_BASE);
    s.text(266, 66, "RB", st_.rb ? 0x0000 : COL_DIM, 2);

    // Analog triggers as vertical bars at the edges.
    const int tH = 110, tY = 100;
    s.fillRect(6, tY, 14, tH, COL_BASE);
    s.fillRect(6, tY + tH - int(st_.lt * tH), 14, int(st_.lt * tH), COL_ACCENT);
    s.text(6, tY + tH + 8, "LT", COL_DIM, 1);
    s.fillRect(W - 20, tY, 14, tH, COL_BASE);
    s.fillRect(W - 20, tY + tH - int(st_.rt * tH), 14, int(st_.rt * tH), COL_ACCENT);
    s.text(W - 20, tY + tH + 8, "RT", COL_DIM, 1);

    // Sticks.
    stick(s, 115, 165, st_.lx, st_.ly, st_.ls);
    stick(s, 253, 165, st_.rx, st_.ry, st_.rs);

    // D-pad cross.
    const int dx = 115, dy = 320;
    s.fillRect(dx - 14, dy - 42, 28, 84, COL_BASE);
    s.fillRect(dx - 42, dy - 14, 84, 28, COL_BASE);
    if (st_.up)    s.fillRect(dx - 14, dy - 42, 28, 28, COL_ACCENT);
    if (st_.down)  s.fillRect(dx - 14, dy + 14, 28, 28, COL_ACCENT);
    if (st_.left)  s.fillRect(dx - 42, dy - 14, 28, 28, COL_ACCENT);
    if (st_.right) s.fillRect(dx + 14, dy - 14, 28, 28, COL_ACCENT);

    // Face buttons (ABXY diamond).
    const int fx = 253, fy = 320;
    faceButton(s, fx,      fy + 30, "A", st_.a, COL_A);
    faceButton(s, fx + 30, fy,      "B", st_.b, COL_B);
    faceButton(s, fx - 30, fy,      "X", st_.x, COL_X);
    faceButton(s, fx,      fy - 30, "Y", st_.y, COL_Y);

    // Center buttons.
    smallButton(s,  98, 408, "VIEW",  st_.view);
    smallButton(s, 156, 408, "XBOX",  st_.xbox);
    smallButton(s, 214, 408, "MENU",  st_.menu);
    smallButton(s, 272, 408, "SHARE", st_.share);

    s.text((W - s.textWidth("press A = rumble", 1)) / 2, 436,
           "press A = rumble", COL_DIM, 1);
}

} // namespace apps
