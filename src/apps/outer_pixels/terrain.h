// The procedural terrain tile behind the low-altitude surface mode: one
// 256x256 heightmap plus a matching colour map, generated from value noise and
// wrapped endlessly in both directions, so flying never reaches an edge.
//
// The tile is generated per planet, seeded from its index and tinted toward
// its colour, so each world reads as its own place. Generation is slow enough
// (65k cells) that the app runs it on a background task while you descend —
// but nothing in here knows about tasks or PSRAM: the buffers are handed in,
// which keeps the noise and the colour bands testable on a PC.
#pragma once

#include <cstdint>

namespace apps::outer {

class Terrain {
public:
    static constexpr int SIZE = 256, MASK = 255;
    static constexpr float HSCALE = 0.36f;   // stored 0..255 -> world height units

    static constexpr int HEIGHT_BYTES = SIZE * SIZE;
    static constexpr int COLOR_BYTES  = SIZE * SIZE * 2;

    // The caller owns the memory (PSRAM on the device, plain arrays in tests).
    void attach(uint8_t* heights, uint16_t* colors) { hm_ = heights; cm_ = colors; }
    [[nodiscard]] bool attached() const { return hm_ && cm_; }

    // Fill the tile for `planet`, tinting the bands toward `base`. Safe to call
    // from another thread as long as nothing samples the tile meanwhile — the
    // app only ever generates a planet it is not currently flying over.
    void generate(int planet, uint16_t base);

    // Sampling. Coordinates wrap, so any world position is valid.
    [[nodiscard]] float    heightAt(int x, int y) const { return hm_[idx(x, y)] * HSCALE; }
    [[nodiscard]] uint16_t colorAt(int x, int y)  const { return cm_[idx(x, y)]; }

    // Which planet the tile currently holds, or -1. Written by generate().
    [[nodiscard]] int held() const { return held_; }

private:
    static int idx(int x, int y) { return (y & MASK) * SIZE + (x & MASK); }

    uint8_t*  hm_ = nullptr;
    uint16_t* cm_ = nullptr;
    // volatile: the generator task writes it, the flight loop polls it to
    // decide whether the tile is ready to drop into.
    volatile int held_ = -1;
};

// Exposed for the cloud transition, which scatters its puffs with the same
// hash so they look like they belong to the same world.
uint32_t hash2d(int x, int y, uint32_t seed);

} // namespace apps::outer
