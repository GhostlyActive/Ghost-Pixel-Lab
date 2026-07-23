#include "sprites.h"

namespace apps::ghost {

void Sprites::reset() {
    for (auto& r : reg_) r = 0;
    collSS_ = 0;
}

void Sprites::write(int reg, uint8_t value) {
    if (reg < 0 || reg >= NUM_REGS) return;
    // The two collision registers are read-only on the chip; a POKE to them
    // does nothing, so a program cannot fake a hit.
    if (reg == R_COLLSS || reg == R_COLLSB) return;
    reg_[reg] = value;
}

uint8_t Sprites::read(int reg) const {
    if (reg == R_COLLSS) { const uint8_t v = collSS_; collSS_ = 0; return v; }
    if (reg == R_COLLSB) return 0;   // sprite/background collision not modelled yet
    if (reg < 0 || reg >= NUM_REGS) return 0;
    return reg_[reg];
}

int Sprites::pixelColor(int i, const uint8_t* ram, int hx, int row) const {
    if (!ram || (unsigned)hx >= 24u || (unsigned)row >= 21u) return -1;
    const uint8_t* d = ram + ram[PTR_BASE + i] * 64;
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

bool Sprites::opaqueAt(int i, const uint8_t* ram, int vx, int vy) const {
    int dx = vx - posX(i);
    int dy = vy - posY(i);
    if (expandX(i)) dx /= 2;
    if (expandY(i)) dy /= 2;
    if ((unsigned)dx >= 24u || (unsigned)dy >= 21u) return false;
    return pixelColor(i, ram, dx, dy) >= 0;
}

void Sprites::updateCollisions(const uint8_t* ram) {
    if (!ram) return;
    uint8_t hits = 0;

    for (int i = 0; i < COUNT; ++i) {
        if (!enabled(i)) continue;
        const int wI = 24 * (expandX(i) ? 2 : 1);
        const int hI = 21 * (expandY(i) ? 2 : 1);
        const int xI = posX(i), yI = posY(i);

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
                    if (opaqueAt(i, ram, vx, vy) && opaqueAt(j, ram, vx, vy)) {
                        hit = true;
                        break;
                    }
            if (hit) hits |= (1 << i) | (1 << j);
        }
    }
    collSS_ |= hits;   // the latch holds every hit until the program reads it
}

} // namespace apps::ghost
