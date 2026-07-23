#include "sid.h"

namespace apps::ghost {

namespace {

// The chip's own attack table, in milliseconds. Decay and release run three
// times slower, which is where the SID's characteristic plucky shape comes from.
constexpr float ATTACK_MS[16] = {
    2, 8, 16, 24, 38, 56, 68, 80, 100, 250, 500, 800, 1000, 3000, 5000, 8000
};

constexpr int REG_VOICE_SIZE = 7;   // freq lo/hi, pw lo/hi, ctrl, ad, sr

// Control-register bits.
constexpr uint8_t GATE = 0x01, SYNC = 0x02, RING = 0x04, TEST = 0x08;
constexpr uint8_t TRI  = 0x10, SAW  = 0x20, PULSE = 0x40, NOISE = 0x80;

// Decay and release are not straight lines on the chip: an exponential ladder
// stretches the step period as the level falls (x2 below 93/255, x4 below
// 54/255, and so on down to x30 near silence). Attack stays linear — that
// asymmetry, fast rise and long dying tail, is the SID's signature shape.
float fallScale(float env) {
    if (env > 93.0f / 255.0f) return 1.0f;
    if (env > 54.0f / 255.0f) return 2.0f;
    if (env > 26.0f / 255.0f) return 4.0f;
    if (env > 14.0f / 255.0f) return 8.0f;
    if (env >  6.0f / 255.0f) return 16.0f;
    return 30.0f;
}

} // namespace

void Sid::setSampleRate(int hz) { rate_ = hz > 0 ? hz : 16000; }

void Sid::reset() {
    for (auto& v : v_) v = Voice{};
    volume_ = 0;
}

// Envelope increment per sample. Attack rises to full scale in ATTACK_MS;
// decay and release fall over three times that, matching the chip's ratio.
float Sid::step(uint8_t nibble, bool attack) const {
    const float ms = ATTACK_MS[nibble & 0x0F] * (attack ? 1.0f : 3.0f);
    const float samples = ms * 0.001f * float(rate_);
    return samples < 1.0f ? 1.0f : 1.0f / samples;
}

void Sid::write(int reg, uint8_t value) {
    if (reg < 0 || reg >= NUM_REGS) return;

    if (reg < VOICES * REG_VOICE_SIZE) {
        Voice& v = v_[reg / REG_VOICE_SIZE];
        switch (reg % REG_VOICE_SIZE) {
        case 0: v.freq = uint16_t((v.freq & 0xFF00) | value); break;
        case 1: v.freq = uint16_t((v.freq & 0x00FF) | (uint16_t(value) << 8)); break;
        case 2: v.pw   = uint16_t((v.pw & 0x0F00) | value); break;
        case 3: v.pw   = uint16_t((v.pw & 0x00FF) | (uint16_t(value & 0x0F) << 8)); break;
        case 4: {
            const bool wasGated = (v.ctrl & GATE) != 0;
            const bool nowGated = (value & GATE) != 0;
            v.ctrl = value;
            if (value & TEST) v.phase = 0;
            // The gate is the only thing that starts or ends a note: opening it
            // restarts the attack even mid-note, closing it drops to release.
            if (nowGated && !wasGated) v.ph = ENV_ATK;
            if (!nowGated && wasGated) v.ph = ENV_REL;
            break;
        }
        case 5: v.ad = value; break;
        case 6: v.sr = value; break;
        default: break;
        }
        return;
    }
    if (reg == 24) volume_ = value & 0x0F;
}

uint8_t Sid::read(int reg) const {
    // Everything but the two oscillator taps is write-only on the real chip.
    if (reg == 27) {
        const uint32_t p = v_[2].phase;
        return uint8_t((p >> 16) & 0xFF);
    }
    if (reg == 28) {
        float e = v_[2].env;
        if (e < 0) e = 0; else if (e > 1) e = 1;
        return uint8_t(e * 255.0f);
    }
    return 0;
}

bool Sid::active() const {
    if (volume_ == 0) return false;
    for (const auto& v : v_) {
        if (v.env > 0.0005f) return true;            // audible right now
        // Silent this instant, but the envelope is still headed somewhere: a
        // gate that just opened rises from zero, and a non-zero sustain holds
        // the voice above it. Both must count, or render() is never called and
        // the envelope can never leave the floor it is sitting on.
        if (v.ph == ENV_ATK) return true;
        if (v.ph != ENV_REL && (v.sr >> 4) != 0) return true;
    }
    // Left over: an open gate with sustain 0, which the chip decays to nothing
    // within milliseconds. The real machine is quiet there, so this one is too.
    return false;
}

// One voice's waveform, 12 bits unsigned. Setting several waveform bits ANDs
// them together, which is what the chip does — that is how the classic
// "combined waveform" timbres arise.
int Sid::waveform(int i) {
    Voice& v = v_[i];
    const uint8_t c = v.ctrl;
    int out = 0x0FFF;
    bool any = false;

    if (c & TRI) {
        // Ring modulation folds the previous voice's sign into the triangle.
        uint32_t p = v.phase;
        if (c & RING) {
            const uint32_t prev = v_[(i + VOICES - 1) % VOICES].phase;
            p ^= (prev & 0x800000);
        }
        const uint32_t folded = (p & 0x800000) ? ~p : p;
        out &= int((folded >> 11) & 0x0FFF);
        any = true;
    }
    if (c & SAW)   { out &= int((v.phase >> 12) & 0x0FFF); any = true; }
    if (c & PULSE) { out &= ((v.phase >> 12) >= v.pw) ? 0x0FFF : 0x0000; any = true; }
    if (c & NOISE) { out &= int((v.noise >> 8) & 0x0FFF); any = true; }
    return any ? out : 0x0800;   // no waveform selected: sit at mid-rail
}

void Sid::render(int16_t* out, int count) {
    if (!out || count <= 0) return;

    for (int n = 0; n < count; ++n) {
        float mix = 0.0f;

        for (int i = 0; i < VOICES; ++i) {
            Voice& v = v_[i];

            // --- oscillator -------------------------------------------------
            if (!(v.ctrl & TEST)) {
                const uint32_t inc = uint32_t((uint64_t(v.freq) * CLOCK) / uint32_t(rate_));
                const uint32_t before = v.phase;
                v.phase = (v.phase + inc) & 0xFFFFFF;

                // Noise shifts on every accumulator bit-19 rise, like the chip.
                if (((before ^ v.phase) & 0x080000) && (v.phase & 0x080000)) {
                    const uint32_t bit = ((v.noise >> 22) ^ (v.noise >> 17)) & 1;
                    v.noise = ((v.noise << 1) | bit) & 0x7FFFFF;
                }
                v.msbRose = !(before & 0x800000) && (v.phase & 0x800000);
            } else {
                v.msbRose = false;   // a held oscillator cannot trigger sync
            }

            // --- envelope ---------------------------------------------------
            const float sustain = float((v.sr >> 4) & 0x0F) / 15.0f;
            switch (v.ph) {
            case ENV_ATK:
                v.env += step(v.ad >> 4, true);
                if (v.env >= 1.0f) { v.env = 1.0f; v.ph = ENV_DEC; }
                break;
            case ENV_DEC:
                v.env -= step(v.ad & 0x0F, false) / fallScale(v.env);
                if (v.env <= sustain) { v.env = sustain; v.ph = ENV_SUS; }
                break;
            case ENV_SUS:
                // Outside attack the envelope only ever falls: lowering the
                // sustain level mid-note drags the voice down to it at the
                // decay rate, raising it does nothing until the gate opens
                // again — exactly the chip's asymmetry.
                if (v.env > sustain) {
                    v.env -= step(v.ad & 0x0F, false) / fallScale(v.env);
                    if (v.env < sustain) v.env = sustain;
                }
                break;
            case ENV_REL:
                v.env -= step(v.sr & 0x0F, false) / fallScale(v.env);
                if (v.env < 0.0f) v.env = 0.0f;
                break;
            }

            if (v.env > 0.0005f) mix += (float(waveform(i)) - 2048.0f) * v.env;
        }

        // Hard sync: the slave resets once, on the exact sample its neighbour's
        // MSB rises — not for a stretch of samples. Applied after all three
        // voices advanced so their order cannot matter.
        for (int i = 0; i < VOICES; ++i)
            if (v_[i].ctrl & SYNC) {
                const Voice& src = v_[(i + VOICES - 1) % VOICES];
                if (src.msbRose) v_[i].phase = 0;
            }

        mix *= float(volume_) / 15.0f * 3.5f;    // headroom for three voices
        if (mix >  32000.0f) mix =  32000.0f;
        if (mix < -32000.0f) mix = -32000.0f;
        out[n] = int16_t(mix);
    }
}

} // namespace apps::ghost
