#include "vic.h"

namespace apps::ghost {

void Vic::reset() {
    for (auto& r : reg_) r = 0;
    // The registers a program expects to find non-zero after power-on, because
    // every listing modifies them with PEEK(...)AND/OR patterns: $D011 = 27
    // (display on, 25 rows), $D016 = 200 (40 columns), $D018 = 21 (screen at
    // $0400, charset at the ROM's $1000).
    reg_[R_CTRL1] = 27;
    reg_[R_CTRL2] = 200;
    reg_[R_MEM]   = 21;
    collSS_ = collSB_ = 0;
    raster_ = 0;
}

void Vic::write(int reg, uint8_t value) {
    if (reg < 0 || reg >= NUM_REGS) return;
    // The two collision registers are read-only on the chip; a POKE to them
    // does nothing, so a program cannot fake a hit.
    if (reg == R_COLLSS || reg == R_COLLSB) return;
    reg_[reg] = value;
}

uint8_t Vic::read(int reg) const {
    if (reg == R_COLLSS) { const uint8_t v = collSS_; collSS_ = 0; return v; }
    if (reg == R_COLLSB) { const uint8_t v = collSB_; collSB_ = 0; return v; }
    // There is no beam here, so the raster register advances on every read.
    // That keeps the idiom honest: WAIT 53266,255 and PEEK-until-equal loops
    // terminate, they just cannot lock onto real screen timing.
    if (reg == R_RASTER) return raster_ += 7;
    if (reg < 0 || reg >= NUM_REGS) return 0;
    return reg_[reg];
}

int Vic::pixelColor(int i, const uint8_t* ram, int hx, int row) const {
    if (!ram || (unsigned)hx >= 24u || (unsigned)row >= 21u) return -1;
    const uint8_t* d = ram + ram[spritePtrBase() + i] * 64;
    const uint8_t byte = d[row * 3 + hx / 8];

    if (!multicolor(i)) {
        const bool set = byte & (0x80 >> (hx & 7));
        return set ? (reg_[R_COL0 + i] & 0x0F) : -1;
    }

    // Multicolour: the 24 bits of a row are read as 12 two-bit pairs, MSB
    // first, each pair two screen pixels wide. The pair value selects the
    // colour source; %00 is transparent.
    const int pairLeft = hx & ~1;                       // even bit index, 0..22
    const uint8_t pb   = d[row * 3 + pairLeft / 8];
    const int val      = (pb >> (6 - (pairLeft & 7))) & 0x3;
    switch (val) {
    case 1:  return reg_[R_MC0] & 0x0F;         // $D025
    case 2:  return reg_[R_COL0 + i] & 0x0F;    // sprite's own colour
    case 3:  return reg_[R_MC1] & 0x0F;         // $D026
    default: return -1;                         // %00 transparent
    }
}

bool Vic::opaqueAt(int i, const uint8_t* ram, int vx, int vy, bool forCollision) const {
    int dx = vx - posX(i);
    int dy = vy - posY(i);
    if (expandX(i)) dx /= 2;
    if (expandY(i)) dy /= 2;
    if ((unsigned)dx >= 24u || (unsigned)dy >= 21u) return false;

    if (forCollision && multicolor(i)) {
        // The chip treats the %01 pair as background: it is drawn, but it
        // neither hits another sprite nor the graphics behind it.
        const uint8_t* d = ram + ram[spritePtrBase() + i] * 64;
        const int pairLeft = dx & ~1;
        const uint8_t pb   = d[dy * 3 + pairLeft / 8];
        return ((pb >> (6 - (pairLeft & 7))) & 0x3) >= 2;
    }
    return pixelColor(i, ram, dx, dy) >= 0;
}

void Vic::updateCollisions(const uint8_t* ram, const uint8_t* fgMask) {
    if (!ram) return;
    if (!den()) return;   // a blanked display fetches nothing, so nothing hits
    uint8_t hitsSS = 0, hitsSB = 0;

    for (int i = 0; i < COUNT; ++i) {
        if (!enabled(i)) continue;
        const int wI = 24 * (expandX(i) ? 2 : 1);
        const int hI = 21 * (expandY(i) ? 2 : 1);
        const int xI = posX(i), yI = posY(i);

        // Sprite/background: walk this sprite's collision-opaque pixels and
        // look them up in the frame's foreground mask (40 bytes per line of
        // the 320x200 field, bit 0x80 first — the layout Screen::render fills).
        if (fgMask) {
            bool hit = false;
            for (int vy = yI; vy < yI + hI && !hit; ++vy) {
                const int ty = vy - 50;
                if ((unsigned)ty >= 200u) continue;
                for (int vx = xI; vx < xI + wI; ++vx) {
                    const int tx = vx - 24;
                    if ((unsigned)tx >= 320u) continue;
                    if (!(fgMask[ty * 40 + (tx >> 3)] & (0x80 >> (tx & 7)))) continue;
                    if (opaqueAt(i, ram, vx, vy, true)) { hit = true; break; }
                }
            }
            if (hit) hitsSB |= (1 << i);
        }

        for (int j = i + 1; j < COUNT; ++j) {
            if (!enabled(j)) continue;
            const int wJ = 24 * (expandX(j) ? 2 : 1);
            const int hJ = 21 * (expandY(j) ? 2 : 1);
            const int xJ = posX(j), yJ = posY(j);

            // Overlap of the two bounding boxes in VIC pixel space; only there
            // can a shared opaque pixel exist.
            const int x0 = xI > xJ ? xI : xJ;
            const int y0 = yI > yJ ? yI : yJ;
            const int x1 = (xI + wI < xJ + wJ) ? xI + wI : xJ + wJ;
            const int y1 = (yI + hI < yJ + hJ) ? yI + hI : yJ + hJ;

            bool hit = false;
            for (int vy = y0; vy < y1 && !hit; ++vy)
                for (int vx = x0; vx < x1; ++vx)
                    if (opaqueAt(i, ram, vx, vy, true) && opaqueAt(j, ram, vx, vy, true)) {
                        hit = true;
                        break;
                    }
            if (hit) hitsSS |= (1 << i) | (1 << j);
        }
    }
    collSS_ |= hitsSS;   // the latches hold every hit until the program reads them
    collSB_ |= hitsSB;
}

} // namespace apps::ghost
