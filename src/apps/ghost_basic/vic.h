// The VIC-II register file at $D000 (53248): the eight sprites (MOBs), the
// display-mode bits and the memory pointers, driven exactly as a C64 listing
// drives them. POKE sets position, colour, shape and mode; PEEK of a collision
// register reads back what has hit what. Positions carry the chip's real
// offset — a sprite at X=24, Y=50 sits in the top-left corner of the text
// area — so coordinates lifted straight from an old listing land where they
// did on the original.
//
// The 63-byte shapes, the shape pointers, custom character sets and the bitmap
// all live in main memory at the original addresses (pointers at 2040, data at
// pointer*64, charset and bitmap wherever $D018 points). This class owns none
// of that: it is handed the 64K RAM at draw and collision time, which keeps it
// pure arithmetic and testable on a PC like the SID.
//
// Not modelled: VIC banking ($DD00 — bank 0 is the machine), smooth scrolling
// (the low bits of $D011/$D016), ECM, and real raster timing — the raster
// register cycles on read so a WAIT on it terminates, nothing more.
#pragma once

#include <cstdint>

namespace apps::ghost {

class Vic {
public:
    static constexpr int COUNT    = 8;
    static constexpr int NUM_REGS = 47;    // $D000..$D02E

    void reset();

    void    write(int reg, uint8_t value);   // reg = address - 53248
    uint8_t read(int reg) const;             // collision registers clear on read

    // Recompute the two collision latches from the current registers, RAM and
    // the frame's foreground mask (as filled by Screen::render; null skips the
    // sprite/background half). Pure arithmetic, no display: the app calls it
    // once a frame, and a test can call it directly and read $D01E/$D01F back.
    void updateCollisions(const uint8_t* ram, const uint8_t* fgMask);

    [[nodiscard]] bool active() const { return (reg_[R_ENABLE] & 0xFF) != 0; }  // any enabled?

    // --- sprite queries the renderer uses ----------------------------------
    bool enabled(int i)    const { return reg_[R_ENABLE] & (1 << i); }
    int  posX(int i)       const { return (((reg_[R_XMSB] >> i) & 1) ? 256 : 0) | reg_[i * 2]; }
    int  posY(int i)       const { return reg_[i * 2 + 1]; }
    bool expandX(int i)    const { return reg_[R_XEXP] & (1 << i); }
    bool expandY(int i)    const { return reg_[R_YEXP] & (1 << i); }
    bool multicolor(int i) const { return reg_[R_MCOL] & (1 << i); }
    bool behindText(int i) const { return reg_[R_PRIO] & (1 << i); }  // $D01B

    // --- display mode and memory pointers ($D011/$D016/$D018) --------------
    bool den()         const { return reg_[R_CTRL1] & 0x10; }   // display enable
    bool bitmapMode()  const { return reg_[R_CTRL1] & 0x20; }   // BMM
    bool multiMode()   const { return reg_[R_CTRL2] & 0x10; }   // MCM
    int  screenBase()  const { return ((reg_[R_MEM] >> 4) & 0x0F) * 1024; }
    int  charsetBase() const { return ((reg_[R_MEM] & 0x0E) >> 1) * 2048; }
    int  bitmapBase()  const { return (reg_[R_MEM] & 0x08) ? 8192 : 0; }
    int  spritePtrBase() const { return screenBase() + 0x3F8; }   // 2040 by default

    // The two extra text backgrounds of multicolour character mode.
    int  bgColor1() const { return reg_[R_BG1] & 0x0F; }   // $D022, bit pair %01
    int  bgColor2() const { return reg_[R_BG2] & 0x0F; }   // $D023, bit pair %10

    // Colour code (0..15) of sprite i's pixel at hires column hx (0..23),
    // row (0..20), or -1 when the pixel is transparent. Understands both the
    // one-bit-per-pixel and the two-bit multicolour encodings.
    int pixelColor(int i, const uint8_t* ram, int hx, int row) const;

private:
    // Whether sprite i covers VIC pixel (vx, vy), undoing X/Y expansion to
    // reach the 24x21 shape grid. In multicolour the %01 pair looks solid but
    // counts as background on the chip, so collisions must ignore it — that is
    // the forCollision flag.
    bool opaqueAt(int i, const uint8_t* ram, int vx, int vy, bool forCollision) const;

    // Register offsets from $D000 (53248), shared by the accessors above and
    // the implementation, so the map lives in exactly one place.
    static constexpr int R_XMSB   = 16;   // $D010: X bit 8, one per sprite
    static constexpr int R_CTRL1  = 17;   // $D011: DEN, bitmap mode
    static constexpr int R_RASTER = 18;   // $D012: raster line (faked, see read)
    static constexpr int R_ENABLE = 21;   // $D015: enable bits
    static constexpr int R_CTRL2  = 22;   // $D016: multicolour mode
    static constexpr int R_YEXP   = 23;   // $D017: vertical expand
    static constexpr int R_MEM    = 24;   // $D018: screen / charset / bitmap base
    static constexpr int R_PRIO   = 27;   // $D01B: sprite behind the text
    static constexpr int R_MCOL   = 28;   // $D01C: multicolour select
    static constexpr int R_XEXP   = 29;   // $D01D: horizontal expand
    static constexpr int R_COLLSS = 30;   // $D01E: sprite/sprite collision
    static constexpr int R_COLLSB = 31;   // $D01F: sprite/background collision
    static constexpr int R_BG1    = 34;   // $D022: multicolour text background 1
    static constexpr int R_BG2    = 35;   // $D023: multicolour text background 2
    static constexpr int R_MC0    = 37;   // $D025: shared sprite multicolour 0
    static constexpr int R_MC1    = 38;   // $D026: shared sprite multicolour 1
    static constexpr int R_COL0   = 39;   // $D027..$D02E: per-sprite colour

    uint8_t         reg_[NUM_REGS] = {};
    mutable uint8_t collSS_ = 0;   // $D01E latch: accumulates until read
    mutable uint8_t collSB_ = 0;   // $D01F latch, same behaviour
    mutable uint8_t raster_ = 0;   // fake raster line, advanced by each read
};

} // namespace apps::ghost
