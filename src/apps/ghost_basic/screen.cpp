#include "screen.h"
#include "vic.h"
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

// The sixteen PETSCII colour codes, in the order the C64 assigns them to the
// CTRL and Commodore number keys. -1 means "not a colour code".
int colorForCode(uint8_t c) {
    switch (c) {
    case 144: return 0;   case   5: return 1;   case  28: return 2;   case 159: return 3;
    case 156: return 4;   case  30: return 5;   case  31: return 6;   case 158: return 7;
    case 129: return 8;   case 149: return 9;   case 150: return 10;  case 151: return 11;
    case 152: return 12;  case 153: return 13;  case 154: return 14;  case 155: return 15;
    default:  return -1;
    }
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
    reverse_ = false;
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

void Screen::put(uint8_t c, bool fromKeyboard) {
    switch (c) {
    case 0x0D:  // RETURN — also cancels reverse mode, like the original
        reverse_ = false;
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
    case 0x93:  // CLR — wipe the screen and go home
        reset();
        return;
    case 0x13:  // HOME
        home();
        return;
    case 0x12: reverse_ = true;  return;   // RVS ON
    case 0x92: reverse_ = false; return;   // RVS OFF
    case 0x11: setCursor(cx_, cy_ + 1); return;   // cursor down
    case 0x91: setCursor(cx_, cy_ - 1); return;   // cursor up
    case 0x1D:                                    // cursor right, wrapping
        if (cx_ + 1 >= COLS) setCursor(0, cy_ + 1); else setCursor(cx_ + 1, cy_);
        return;
    case 0x9D:                                    // cursor left, wrapping
        if (cx_ == 0) setCursor(COLS - 1, cy_ - 1); else setCursor(cx_ - 1, cy_);
        return;
    default:
        break;
    }

    if (const int col = colorForCode(c); col >= 0) { text_ = uint8_t(col); return; }

    poke(cx_, cy_, uint8_t(petscii::toScreenCode(c) | (reverse_ ? 0x80 : 0x00)), text_);
    if (++cx_ >= COLS) {
        if (fromKeyboard && cont_[cy_]) {
            // Second row of a logical line is full: 80 characters is the limit.
            // Moving to a third row here would strand the line, because RETURN
            // reads the line the cursor is on and that row is a fresh one.
            cx_ = COLS - 1;
        } else {
            // Auto-wrap: link the next row to this one, but cap a logical line
            // at two rows so a third wrapped row starts a new one.
            const bool link = !cont_[cy_];
            newLine();
            cont_[cy_] = link;
        }
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
                      uint8_t code, uint8_t color, bool cursor,
                      const Vic* vic, const uint8_t* ram) const {
    const int sx0 = col * petscii::CW;   // this cell's top-left source pixel
    const int sy0 = row * petscii::CH;

    // The cursor is a glyph swap, not a paint effect: the KERNAL flips bit 7
    // of the code under it. With the ROM font that toggles reverse video; in a
    // custom charset it shows glyph+128, exactly like the original.
    if (cursor) code ^= 0x80;

    // Glyph source. $1000 and $1800 are where the VIC sees the character ROM,
    // so those two values mean the built-in font; anything else is a custom
    // set the program poked into RAM. The ROM's upper half is the bitwise
    // inverse of the lower — that is all reverse video is.
    const int base = vic ? vic->charsetBase() : 0x1000;
    const bool rom = !ram || base == 0x1000 || base == 0x1800;

    // Multicolour text: colour-RAM bit 3 switches the cell, the glyph bits
    // pair up two pixels wide, and only %10/%11 count as foreground.
    const bool mc = vic && vic->multiMode() && (color & 0x08);
    const uint16_t mcPal[4] = { PALETTE[bg_], PALETTE[vic ? vic->bgColor1() : 0],
                                PALETTE[vic ? vic->bgColor2() : 0], PALETTE[color & 0x07] };

    const uint16_t ink = PALETTE[color & 0x0F];

    for (int r = 0; r < petscii::CH; ++r) {
        uint8_t eff;
        if (rom) {
            const uint8_t bits = petscii::FONT[code & 0x7F][r];
            eff = (code & 0x80) ? uint8_t(~bits) : bits;
        } else {
            eff = ram[base + code * 8 + r];
        }

        const int y0 = OY + (sy0 + r) * ZN / ZD;
        const int y1 = OY + (sy0 + r + 1) * ZN / ZD;
        uint8_t& mask = fgmask_[(sy0 + r) * COLS + col];

        if (mc) {
            // Draw the four pair values as runs; adjacent equal pairs merge.
            mask = 0;
            for (int p = 0; p < 8; ) {
                const int v = (eff >> (6 - p)) & 0x3;
                int e = p;
                while (e < 8 && ((eff >> (6 - e)) & 0x3) == v) e += 2;
                if (v >= 2) mask |= uint8_t((0xFF00 >> (e - p)) & 0xFF) >> p;
                if (v != 0) {   // %00 is the already-painted background
                    const int x0 = OX + (sx0 + p) * ZN / ZD;
                    const int x1 = OX + (sx0 + e) * ZN / ZD;
                    fillL(s, x0, y0, x1 - x0, y1 - y0, mcPal[v]);
                }
                p = e;
            }
            continue;
        }

        mask = eff;
        if (!eff) continue;
        // Coalesce runs of set bits into one rectangle instead of per-pixel
        // writes — a scaled glyph is mostly short horizontal runs.
        for (int c = 0; c < petscii::CW; ) {
            if (!(eff & (0x80 >> c))) { ++c; continue; }
            int e = c;
            while (e < petscii::CW && (eff & (0x80 >> e))) ++e;
            const int x0 = OX + (sx0 + c) * ZN / ZD;
            const int x1 = OX + (sx0 + e) * ZN / ZD;
            fillL(s, x0, y0, x1 - x0, y1 - y0, ink);
            c = e;
        }
    }
}

// The two bitmap modes. Every pixel of the 320x200 window is owned by the
// bitmap, so nothing of the text layer shows through: per 8x8 cell the screen
// matrix supplies the colours (its "codes" become colour nibbles) and the
// bitmap byte supplies the pixels. Runs of equal colour collapse into single
// rectangles, the same trick the glyph path uses.
void Screen::drawBitmap(Surface& s, const Vic& vic, const uint8_t* ram) const {
    const int bmBase = vic.bitmapBase();
    const int scBase = vic.screenBase();
    const bool mc    = vic.multiMode();

    for (int y = 0; y < PIXH; ++y) {
        const int cellRow = y >> 3;
        const int y0 = OY + y * ZN / ZD;
        const int y1 = OY + (y + 1) * ZN / ZD;

        for (int col = 0; col < COLS; ++col) {
            const int cell = cellRow * COLS + col;
            const uint8_t bits = ram[(bmBase + cellRow * 320 + col * 8 + (y & 7)) & 0xFFFF];
            // The colour cell: our matrix when the screen sits at the default
            // $0400, plain RAM when the program moved it.
            const uint8_t cb = (scBase == 0x0400) ? code_[cell]
                                                  : ram[(scBase + cell) & 0xFFFF];
            uint8_t& mask = fgmask_[y * COLS + col];
            const int sx0 = col * petscii::CW;

            if (!mc) {
                mask = bits;
                const uint16_t fg = PALETTE[cb >> 4], bg = PALETTE[cb & 0x0F];
                for (int c = 0; c < 8; ) {
                    const bool set = bits & (0x80 >> c);
                    int e = c;
                    while (e < 8 && bool(bits & (0x80 >> e)) == set) ++e;
                    const int x0 = OX + (sx0 + c) * ZN / ZD;
                    const int x1 = OX + (sx0 + e) * ZN / ZD;
                    fillL(s, x0, y0, x1 - x0, y1 - y0, set ? fg : bg);
                    c = e;
                }
            } else {
                // %00 background, %01/%10 the colour cell's nibbles, %11 the
                // colour RAM — and as everywhere, only %10/%11 are foreground.
                const uint16_t pal[4] = { PALETTE[bg_], PALETTE[cb >> 4],
                                          PALETTE[cb & 0x0F], PALETTE[color_[cell] & 0x0F] };
                mask = 0;
                for (int p = 0; p < 8; ) {
                    const int v = (bits >> (6 - p)) & 0x3;
                    int e = p;
                    while (e < 8 && ((bits >> (6 - e)) & 0x3) == v) e += 2;
                    if (v >= 2) mask |= uint8_t((0xFF00 >> (e - p)) & 0xFF) >> p;
                    const int x0 = OX + (sx0 + p) * ZN / ZD;
                    const int x1 = OX + (sx0 + e) * ZN / ZD;
                    fillL(s, x0, y0, x1 - x0, y1 - y0, pal[v]);
                    p = e;
                }
            }
        }
    }
}

void Screen::render(Surface& s, bool cursorOn, const Vic* vic, const uint8_t* ram) const {
    s.clear(PALETTE[border_]);

    // Display off: the whole screen shows the border, nothing is fetched —
    // which is also why a blanked frame can never collide (see Vic).
    if (vic && !vic->den()) {
        std::memset(fgmask_, 0, sizeof fgmask_);
        return;
    }

    if (vic && ram && vic->bitmapMode()) {
        drawBitmap(s, *vic, ram);
        return;
    }

    fillL(s, OX, OY, TEXT_W, TEXT_H, PALETTE[bg_]);
    for (int y = 0; y < ROWS; ++y) {
        for (int x = 0; x < COLS; ++x) {
            const int i = index(x, y);
            const bool cursor = cursorOn && x == cx_ && y == cy_;
            drawCell(s, x, y, code_[i], color_[i], cursor, vic, ram);
        }
    }
}

void Screen::renderSprites(Surface& s, const Vic& vic, const uint8_t* ram) const {
    if (!ram) return;

    // One source pixel of the 320x200 field, expanded to a zoom block and
    // rotated the same way the characters are — so sprites stay pixel-aligned
    // with the text underneath them.
    auto plot = [&](int sx, int sy, uint16_t color) {
        const int x0 = OX + sx * ZN / ZD, x1 = OX + (sx + 1) * ZN / ZD;
        const int y0 = OY + sy * ZN / ZD, y1 = OY + (sy + 1) * ZN / ZD;
        fillL(s, x0, y0, x1 - x0, y1 - y0, color);
    };

    // Sprite 0 has the highest priority, so paint 7 first and let 0 land on top.
    for (int i = Vic::COUNT - 1; i >= 0; --i) {
        if (!vic.enabled(i)) continue;
        const bool xe = vic.expandX(i), ye = vic.expandY(i);
        const bool behind = vic.behindText(i);
        const int physW = 24 * (xe ? 2 : 1);
        const int physH = 21 * (ye ? 2 : 1);
        const int left = vic.posX(i), top = vic.posY(i);

        for (int dy = 0; dy < physH; ++dy) {
            const int ty = top + dy - 50;                 // into the 0..199 field
            if ((unsigned)ty >= (unsigned)PIXH) continue;
            const int row = ye ? dy / 2 : dy;
            for (int dx = 0; dx < physW; ++dx) {
                const int hx = xe ? dx / 2 : dx;
                const int col = vic.pixelColor(i, ram, hx, row);
                if (col < 0) continue;
                const int tx = left + dx - 24;             // into the 0..319 field
                if ((unsigned)tx >= (unsigned)PIXW) continue;
                // $D01B: the sprite slips behind the graphics — its pixels
                // only reach the glass where the frame has background.
                if (behind && (fgmask_[ty * COLS + (tx >> 3)] & (0x80 >> (tx & 7))))
                    continue;
                plot(tx, ty, PALETTE[col]);
            }
        }
    }
}

} // namespace apps::ghost
