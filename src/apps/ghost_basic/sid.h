// A 6581 SID in software: three voices, four waveforms, an ADSR envelope each,
// plus ring modulation and oscillator sync. Registers are written through the
// same POKE addresses a real listing uses (54272 upwards).
//
// This is pure arithmetic — no audio driver, no Arduino. The app pulls blocks
// of samples out of render() and hands them to the speaker, which keeps this
// file testable on a PC like the rest of the interpreter.
//
// Not modelled: the analogue filter. Its cutoff varied so much between chips
// that no two C64s sounded alike, and leaving it out costs far less character
// than a bad approximation would.
#pragma once

#include <cstdint>

namespace apps::ghost {

class Sid {
public:
    static constexpr int VOICES    = 3;
    static constexpr int NUM_REGS  = 29;   // $D400..$D41C
    static constexpr uint32_t CLOCK = 985248;   // PAL system clock

    void setSampleRate(int hz);
    void reset();

    void    write(int reg, uint8_t value);
    uint8_t read(int reg) const;      // only osc3 / env3 read back, as on the chip

    // Mono samples for the speaker. Silent when every gate is closed, so a
    // program that makes no sound costs nothing but the loop.
    void render(int16_t* out, int count);

    [[nodiscard]] bool active() const;   // any voice still sounding?

private:
    // Prefixed because Arduino defines DEC as a macro; a bare DEC here expands
    // to a number and breaks the enum in a spectacularly unhelpful way.
    enum Phase : uint8_t { ENV_REL = 0, ENV_ATK, ENV_DEC, ENV_SUS };

    struct Voice {
        uint16_t freq = 0;
        uint16_t pw   = 0;         // 12-bit pulse width
        uint8_t  ctrl = 0;
        uint8_t  ad = 0, sr = 0;

        uint32_t phase = 0;        // 24-bit accumulator, like the original
        uint32_t noise = 0x7FFFF8;
        bool     lastMsb = false;
        float    env = 0;
        Phase    ph = ENV_REL;
    };

    float step(uint8_t nibble, bool attack) const;
    int   waveform(int i);

    Voice   v_[VOICES];
    uint8_t volume_ = 0;
    int     rate_   = 16000;
};

} // namespace apps::ghost
