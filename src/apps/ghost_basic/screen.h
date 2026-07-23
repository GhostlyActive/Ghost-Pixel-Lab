// The text screen: a 40x25 grid of screen codes plus per-cell colour RAM,
// a single global background, and a border. Knows nothing about the display —
// it only holds the matrix and exposes a printing/cursor API. render() is the
// one place that turns the matrix into pixels, so the model stays testable and
// the renderer stays swappable.
//
// put() understands the PETSCII control codes a C64 program embeds in strings:
// RETURN, DELETE, CLR/HOME, the four cursor moves, reverse-video on/off and all
// sixteen colour codes. That is what makes PRINT CHR$(147) clear the screen and
// PRINT CHR$(28);"..." come out red. Everything else prints through the
// PETSCII->screen-code map.
#pragma once

#include "board/surface.h"
#include "petscii.h"
#include "palette.h"

#include <cstdint>

namespace apps::ghost {

class Vic;

class Screen {
public:
    static constexpr int COLS = 40;
    static constexpr int ROWS = 25;
    static constexpr int PIXW = COLS * petscii::CW;  // 320
    static constexpr int PIXH = ROWS * petscii::CH;  // 200

    // Wipe to spaces in the current text colour, cursor home.
    void reset();

    // Cursor.
    void home()                      { cx_ = 0; cy_ = 0; }
    void setCursor(int x, int y);
    int  cursorX() const             { return cx_; }
    int  cursorY() const             { return cy_; }

    // Colours (C64 colour codes 0..15).
    void setTextColor(uint8_t c)     { text_ = c & 0x0F; }
    void setBackground(uint8_t c)    { bg_ = c & 0x0F; }
    void setBorder(uint8_t c)        { border_ = c & 0x0F; }
    uint8_t textColor() const        { return text_; }

    // Printing.
    // fromKeyboard caps a typed line at the 80-character logical line; program
    // output passes false and wraps across as many rows as it needs.
    void put(uint8_t petscii, bool fromKeyboard = false);
    void print(const char* s);                 // convenience for banners
    void newLine();

    // Direct matrix access (screen code carries reverse-video in bit 7).
    void poke(int x, int y, uint8_t screenCode, uint8_t color);
    uint8_t peekCode(int x, int y) const;

    // Memory-mapped access, the way a C64 program reaches the screen: the
    // 1000 cells behind POKE 1024+n, the colour nibbles behind POKE 55296+n,
    // and the two VIC colour registers. Offsets outside the grid are ignored,
    // so a runaway POKE loop cannot corrupt anything.
    static constexpr int CELLS = ROWS * COLS;   // 1000
    void    pokeScreen(int offset, uint8_t screenCode);
    uint8_t peekScreen(int offset) const;
    void    pokeColor(int offset, uint8_t color);
    uint8_t peekColor(int offset) const;
    uint8_t background() const { return bg_; }
    uint8_t border() const     { return border_; }

    // Read a single physical row back as PETSCII text (trailing spaces
    // trimmed, NUL-terminated). Returns the character count.
    int readLine(int row, char* out, int outSize) const;

    // Read the whole *logical* line containing `row` — a line that wrapped past
    // column 40 spans two physical rows (up to 80 chars), exactly like the C64.
    // Returns the index of the line's last physical row (for cursor placement).
    int readLogicalLine(int row, char* out, int outSize) const;

    // Paint the machine's picture into the surface. cursorOn toggles the
    // reverse block at the cursor cell so the caller can blink it. With a Vic
    // and the 64K RAM the display honours the chip's mode bits — custom
    // character sets, the two bitmap modes, multicolour text, DEN — and fills
    // the foreground mask as it goes; without them it is the plain text matrix
    // (which is what the interpreter tests use).
    void render(board::gfx::Surface& s, bool cursorOn,
                const Vic* vic = nullptr, const uint8_t* ram = nullptr) const;

    // Composite the eight sprites over the graphics, using the same origin,
    // zoom and rotation as the characters so a sprite lands exactly where its
    // VIC coordinates put it. Honours the behind-the-text bit against the
    // foreground mask of the frame render() just produced.
    void renderSprites(board::gfx::Surface& s, const Vic& vic,
                       const uint8_t* ram) const;

    // The frame's foreground pixels, 40 bytes per line of the 320x200 field
    // (bit 0x80 = leftmost). This is what the VIC calls "foreground" for
    // priority and collisions: set glyph bits, bitmap 1-bits, and the %10/%11
    // pairs of the multicolour modes. Filled by render().
    const uint8_t* fgMask() const { return fgmask_; }

private:
    int  index(int x, int y) const   { return y * COLS + x; }
    void scrollUp();
    void drawCell(board::gfx::Surface& s, int col, int row,
                  uint8_t code, uint8_t color, bool cursor,
                  const Vic* vic, const uint8_t* ram) const;
    void drawBitmap(board::gfx::Surface& s, const Vic& vic,
                    const uint8_t* ram) const;

    uint8_t code_[ROWS * COLS] = {};   // screen codes (bit 7 = reverse video)
    uint8_t color_[ROWS * COLS] = {};  // colour RAM, 0..15
    // Foreground mask of the last rendered frame (see fgMask). Mutable because
    // it is a render product, not screen state.
    mutable uint8_t fgmask_[PIXH * COLS] = {};
    bool    cont_[ROWS] = {};          // row continues the logical line above it
    int     cx_ = 0, cy_ = 0;          // cursor cell
    bool    reverse_ = false;          // RVS mode, cleared by RETURN like the original
    uint8_t text_   = COL_LTBLUE;      // current text colour
    uint8_t bg_     = COL_BLUE;        // global background
    uint8_t border_ = COL_LTBLUE;      // border
};

} // namespace apps::ghost
