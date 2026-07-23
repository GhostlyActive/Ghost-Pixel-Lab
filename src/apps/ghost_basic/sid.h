// A 6581 SID in software: three voices, four waveforms, an ADSR envelope each,
// plus ring modulation and oscillator sync. Registers are written through the
// same POKE addresses a real listing uses (54272 upwards).
//
// This is pure arithmetic — no audio driver, no Arduino. The app pulls blocks
// of samples out of render() and hands them to the speaker, which keeps this
// file testable on a PC like the rest of the interpreter.
//
// The analogue filter is a digital state-variable stage: per-voice routing,
// low/band/high-pass select, 11-bit cutoff and the resonance register all
// behave as on the chip, including the bit that mutes voice 3 so it can serve
// as a silent LFO. One honest caveat: the real 6581's cutoff curve varied so
// much between chips that no two C64s sounded alike — the mapping here
// (roughly 30 Hz to 6 kHz, resonance mild like the original) is one plausible
// chip, not every chip.
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
        bool     msbRose = false;  // accumulator MSB went 0->1 this sample (drives sync)
        float    env = 0;
        Phase    ph = ENV_REL;
    };

    float step(uint8_t nibble, bool attack) const;
    int   waveform(int i);

    Voice   v_[VOICES];
    uint8_t volume_ = 0;
    int     rate_   = 16000;

    // Filter registers ($D415..$D418) and the state-variable integrators.
    uint8_t fcLo_    = 0;      // cutoff bits 0..2
    uint8_t fcHi_    = 0;      // cutoff bits 3..10
    uint8_t resFilt_ = 0;      // resonance (high nibble) / routing (low)
    uint8_t modeVol_ = 0;      // LP/BP/HP select, voice-3-off, volume
    float   fIc1_ = 0, fIc2_ = 0;
};

} // namespace apps::ghost
