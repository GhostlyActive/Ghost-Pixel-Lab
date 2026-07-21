#include "screen.h"
#include "board/display.h"

#include <cstring>

namespace apps::ghost {

using board::gfx::Surface;

namespace {
// Screen orientation. LANDSCAPE rotates the panel 90 degrees so the machine is
// seen the way a C64 actually was — wide, like a TV — and zooms 1.4x to fill
// the long side. Portrait keeps crisp 1:1 pixels inside a larger border.
// This is the one line to flip.
constexpr bool LANDSCAPE = false;

constexpr int LW = LANDSCAPE ? board::display::HEIGHT : board::display::WIDTH;
constexpr int LH = LANDSCAPE ? board::display::WIDTH  : board::display::HEIGHT;

// Zoom as a rational, so the 8x8 cells tile exactly with no gaps or overlaps.
// Landscape 7/5: 320*7/5 = 448 fills the width exactly, pixels stay square,
// and 2 of every 5 source pixels are drawn double-width. Portrait 1/1: the
// panel is only 368 wide, so anything above 1x would be a ragged 1.15x.
constexpr int ZN = LANDSCAPE ? 7 : 1;
constexpr int ZD = LANDSCAPE ? 5 : 1;

constexpr int TEXT_W = Screen::PIXW * ZN / ZD;
constexpr int TEXT_H = Screen::PIXH * ZN / ZD;
constexpr int OX = (LW - TEXT_W) / 2;
constexpr int OY = (LH - TEXT_H) / 2;

// Landscape (lx, ly) -> panel, rotated 90 degrees. A rectangle stays a
// rectangle, so this costs nothing beyond swapping the arguments. In portrait
// the branch compiles away entirely.
inline void fillL(board::gfx::Surface& s, int lx, int ly, int w, int h, uint16_t c) {
    if constexpr (LANDSCAPE) s.fillRect(board::display::WIDTH - ly - h, lx, h, w, c);
    else                     s.fillRect(lx, ly, w, h, c);
}

// Screen code -> PETSCII, for reading a line back off the screen. Shared by
// readLine and readLogicalLine so the mapping can never drift between them.
// Note screen code 30 is the up-arrow, which is BASIC's exponent operator '^'.
char screenToPetscii(uint8_t sc) {
    sc &= 0x7F;
    if (sc == 0)              return '@';
    if (sc >= 1 && sc <= 26)  return char('A' + sc - 1);
    if (sc == 27)             return '[';
    if (sc == 29)             return ']';
    if (sc == 30)             return '^';
    if (sc >= 32 && sc <= 63) return char(sc);
    // Graphics characters have no BASIC source form; a line read back off the
    // screen sees them as blanks rather than as bogus tokens.
    return ' ';
}
}

void Screen::reset() {
    for (int i = 0; i < ROWS * COLS; ++i) {
        code_[i]  = 32;      // space
        color_[i] = text_;
    }
    for (int r = 0; r < ROWS; ++r) cont_[r] = false;
    cx_ = cy_ = 0;
}

void Screen::setCursor(int x, int y) {
    if (x < 0) x = 0; else if (x >= COLS) x = COLS - 1;
    if (y < 0) y = 0; else if (y >= ROWS) y = ROWS - 1;
    cx_ = x; cy_ = y;
}

void Screen::poke(int x, int y, uint8_t screenCode, uint8_t color) {
    if ((unsigned)x >= COLS || (unsigned)y >= ROWS) return;
    const int i = index(x, y);
    code_[i]  = screenCode;
    color_[i] = color & 0x0F;
}

uint8_t Screen::peekCode(int x, int y) const {
    if ((unsigned)x >= COLS || (unsigned)y >= ROWS) return 32;
    return code_[index(x, y)];
}

void Screen::pokeScreen(int offset, uint8_t screenCode) {
    if ((unsigned)offset < (unsigned)CELLS) code_[offset] = screenCode;
}

uint8_t Screen::peekScreen(int offset) const {
    return (unsigned)offset < (unsigned)CELLS ? code_[offset] : 32;
}

void Screen::pokeColor(int offset, uint8_t color) {
    if ((unsigned)offset < (unsigned)CELLS) color_[offset] = color & 0x0F;
}

uint8_t Screen::peekColor(int offset) const {
    return (unsigned)offset < (unsigned)CELLS ? color_[offset] : 0;
}

void Screen::newLine() {
    cx_ = 0;
    if (++cy_ >= ROWS) { scrollUp(); cy_ = ROWS - 1; }
    cont_[cy_] = false;   // an explicit new line starts a fresh logical line
}

void Screen::put(uint8_t c) {
    switch (c) {
    case 0x0D:  // RETURN
        newLine();
        return;
    case 0x14:  // DELETE / backspace
        if (cx_ > 0) {
            --cx_;
        } else if (cy_ > 0) {
            --cy_; cx_ = COLS - 1;
        }
        poke(cx_, cy_, 32, text_);
        return;
    default:
        break;
    }

    poke(cx_, cy_, petscii::toScreenCode(c), text_);
    if (++cx_ >= COLS) {
        // Auto-wrap: link the next row to this one, but cap a logical line at
        // two physical rows (so a 3rd wrapped row begins a new logical line).
        const bool link = !cont_[cy_];
        newLine();
        cont_[cy_] = link;
    }
}

void Screen::print(const char* s) {
    while (*s) {
        const char c = *s++;
        put(c == '\n' ? 0x0D : uint8_t(c));
    }
}

void Screen::scrollUp() {
    std::memmove(&code_[0],  &code_[COLS],  (ROWS - 1) * COLS);
    std::memmove(&color_[0], &color_[COLS], (ROWS - 1) * COLS);
    const int last = (ROWS - 1) * COLS;
    for (int i = 0; i < COLS; ++i) {
        code_[last + i]  = 32;
        color_[last + i] = text_;
    }
    for (int r = 0; r < ROWS - 1; ++r) cont_[r] = cont_[r + 1];
    cont_[ROWS - 1] = false;
    cont_[0] = false;   // a continuation whose start scrolled off is now a start
}

int Screen::readLine(int row, char* out, int outSize) const {
    if ((unsigned)row >= ROWS || outSize <= 0) { if (outSize > 0) out[0] = 0; return 0; }

    // PETSCII back-conversion for the printable subset we render.
    int n = 0;
    const int limit = (outSize - 1 < COLS) ? outSize - 1 : COLS;
    for (int x = 0; x < limit; ++x) out[n++] = screenToPetscii(code_[index(x, row)]);
    while (n > 0 && out[n - 1] == ' ') --n;   // trim trailing spaces
    out[n] = 0;
    return n;
}

int Screen::readLogicalLine(int row, char* out, int outSize) const {
    if ((unsigned)row >= ROWS || outSize <= 0) { if (outSize > 0) out[0] = 0; return row; }

    int start = row;
    while (start > 0 && cont_[start]) --start;       // walk up to the logical start
    int end = start;
    while (end + 1 < ROWS && cont_[end + 1]) ++end;  // and down to its last row

    int n = 0;
    const int cap = outSize - 1;
    for (int r = start; r <= end && n < cap; ++r)
        for (int x = 0; x < COLS && n < cap; ++x)
            out[n++] = screenToPetscii(code_[index(x, r)]);
    while (n > 0 && out[n - 1] == ' ') --n;           // trim trailing spaces
    out[n] = 0;
    return end;
}

void Screen::drawCell(Surface& s, int col, int row,
                      uint8_t code, uint8_t color, bool reverse) const {
    const uint16_t fg  = PALETTE[color & 0x0F];
    const uint16_t bgc = PALETTE[bg_];
    const uint8_t* g   = petscii::FONT[code & 0x7F];

    const int sx0 = col * petscii::CW;   // this cell's top-left source pixel
    const int sy0 = row * petscii::CH;

    if (reverse) {
        const int x0 = OX + sx0 * ZN / ZD, x1 = OX + (sx0 + petscii::CW) * ZN / ZD;
        const int y0 = OY + sy0 * ZN / ZD, y1 = OY + (sy0 + petscii::CH) * ZN / ZD;
        fillL(s, x0, y0, x1 - x0, y1 - y0, fg);
    }

    const uint16_t ink = reverse ? bgc : fg;
    for (int r = 0; r < petscii::CH; ++r) {
        const uint8_t bits = g[r];
        if (!bits) continue;
        const int y0 = OY + (sy0 + r) * ZN / ZD;
        const int y1 = OY + (sy0 + r + 1) * ZN / ZD;
        // Coalesce runs of set bits into one rectangle instead of per-pixel
        // writes — a scaled glyph is mostly short horizontal runs.
        for (int c = 0; c < petscii::CW; ) {
            if (!(bits & (0x80 >> c))) { ++c; continue; }
            int e = c;
            while (e < petscii::CW && (bits & (0x80 >> e))) ++e;
            const int x0 = OX + (sx0 + c) * ZN / ZD;
            const int x1 = OX + (sx0 + e) * ZN / ZD;
            fillL(s, x0, y0, x1 - x0, y1 - y0, ink);
            c = e;
        }
    }
}

void Screen::render(Surface& s, bool cursorOn) const {
    s.clear(PALETTE[border_]);
    fillL(s, OX, OY, TEXT_W, TEXT_H, PALETTE[bg_]);

    for (int y = 0; y < ROWS; ++y) {
        for (int x = 0; x < COLS; ++x) {
            const int i = index(x, y);
            uint8_t code = code_[i];
            bool reverse = code & 0x80;
            if (cursorOn && x == cx_ && y == cy_) reverse = !reverse;
            drawCell(s, x, y, code, color_[i], reverse);
        }
    }
}

} // namespace apps::ghost
