// The VIC-II's eight sprites (MOBs), driven exactly as a C64 listing drives
// them: POKE to $D000.. sets position, colour, shape and the mode bits, and
// PEEK of the collision register reads back what has hit what. Positions carry
// the chip's real offset — a sprite at X=24, Y=50 sits in the top-left corner
// of the text area — so coordinates lifted straight from an old listing land
// where they did on the original.
//
// The 63-byte shapes and the shape pointers live in main memory, at the same
// addresses (pointers at 2040, data at pointer*64). This class owns none of
// that: it is handed the 64K RAM at draw and collision time, which keeps it
// pure arithmetic and testable on a PC like the SID.
//
// Not modelled: sprite/background priority ($D01B) — sprites always draw in
// front — and sprite/background collision ($D01F), which needs the character
// foreground and reads back as zero for now. Sprite/sprite collision is exact.
#pragma once

#include <cstdint>

namespace apps::ghost {

class Sprites {
public:
    static constexpr int COUNT    = 8;
    static constexpr int NUM_REGS = 47;    // $D000..$D02E
    static constexpr int PTR_BASE = 2040;  // $07F8: shape pointers (screen at $0400)

    void reset();

    void    write(int reg, uint8_t value);   // reg = address - 53248
    uint8_t read(int reg) const;             // the collision register clears on read

    // Recompute sprite/sprite collisions from the current registers and RAM.
    // Pure arithmetic, no display: the app calls it once a frame, and a test
    // can call it directly and read $D01E back.
    void updateCollisions(const uint8_t* ram);

    [[nodiscard]] bool active() const { return (reg_[21] & 0xFF) != 0; }  // any enabled?

    // --- queries the renderer uses -----------------------------------------
    bool enabled(int i)    const { return reg_[R_ENABLE] & (1 << i); }
    int  posX(int i)       const { return (((reg_[R_XMSB] >> i) & 1) ? 256 : 0) | reg_[i * 2]; }
    int  posY(int i)       const { return reg_[i * 2 + 1]; }
    bool expandX(int i)    const { return reg_[R_XEXP] & (1 << i); }
    bool expandY(int i)    const { return reg_[R_YEXP] & (1 << i); }
    bool multicolor(int i) const { return reg_[R_MCOL] & (1 << i); }

    // Colour code (0..15) of sprite i's pixel at hires column hx (0..23),
    // row (0..20), or -1 when the pixel is transparent. Understands both the
    // one-bit-per-pixel and the two-bit multicolour encodings.
    int pixelColor(int i, const uint8_t* ram, int hx, int row) const;

private:
    // Whether sprite i covers VIC pixel (vx, vy) with a non-transparent pixel,
    // undoing X/Y expansion to reach the 24x21 shape grid.
    bool opaqueAt(int i, const uint8_t* ram, int vx, int vy) const;

    // Register offsets from $D000 (53248), shared by the accessors above and
    // the implementation, so the map lives in exactly one place.
    static constexpr int R_XMSB   = 16;   // $D010: X bit 8, one per sprite
    static constexpr int R_ENABLE = 21;   // $D015: enable bits
    static constexpr int R_YEXP   = 23;   // $D017: vertical expand
    static constexpr int R_MCOL   = 28;   // $D01C: multicolour select
    static constexpr int R_XEXP   = 29;   // $D01D: horizontal expand
    static constexpr int R_COLLSS = 30;   // $D01E: sprite/sprite collision
    static constexpr int R_COLLSB = 31;   // $D01F: sprite/background collision
    static constexpr int R_MC0    = 37;   // $D025: shared multicolour 0
    static constexpr int R_MC1    = 38;   // $D026: shared multicolour 1
    static constexpr int R_COL0   = 39;   // $D027..$D02E: per-sprite colour

    uint8_t         reg_[NUM_REGS] = {};
    mutable uint8_t collSS_ = 0;   // $D01E latch: accumulates until read
};

} // namespace apps::ghost
