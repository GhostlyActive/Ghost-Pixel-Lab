#include "apps/ghost_basic.h"
#include "core/keyboard.h"
#include "core/files.h"
#include "board/audio.h"

#include <Arduino.h>
#include <cstdio>
#include <cstring>

namespace apps {

using apps::ghost::Screen;
using apps::ghost::Basic;

namespace {
constexpr float BLINK_PERIOD = 0.5f;
constexpr uint32_t SAMPLE_RATE = 16000;
constexpr size_t   CHUNK_MAX   = 2048;

// SAVE / LOAD / DIRECTORY, backed by /GHOST/BASIC on the SD card (or flash).
// Programs are plain text so they can be read on a PC too.
class ProgramFiles final : public apps::ghost::Files {
public:
    bool save(const char* name, const std::string& text) override {
        fs::FS* fs = core::files::fs();
        if (!fs) return false;
        char p[128]; pathFor(name, p, sizeof p);
        File f = fs->open(p, "w");
        if (!f) return false;
        const bool ok = f.print(text.c_str()) == int(text.size());
        f.close();
        return ok;
    }

    bool load(const char* name, std::string& out) override {
        fs::FS* fs = core::files::fs();
        if (!fs) return false;
        char p[128]; pathFor(name, p, sizeof p);
        File f = fs->open(p, "r");
        if (!f || f.isDirectory()) return false;
        out.clear();
        while (f.available()) out += char(f.read());
        f.close();
        return true;
    }

    bool list(std::vector<Entry>& out) override {
        fs::FS* fs = core::files::fs();
        if (!fs) return false;
        File dir = fs->open(core::files::basicDir());
        if (!dir || !dir.isDirectory()) return false;
        for (File e = dir.openNextFile(); e; e = dir.openNextFile()) {
            if (e.isDirectory()) { e.close(); continue; }
            std::string n = e.name();
            const std::size_t dot = n.rfind(".PRG");
            if (dot != std::string::npos) n.erase(dot);
            out.push_back(Entry{n, uint32_t(e.size())});
            e.close();
        }
        dir.close();
        return true;
    }

    bool remove(const char* name) override {
        fs::FS* fs = core::files::fs();
        if (!fs) return false;
        char p[128]; pathFor(name, p, sizeof p);
        return fs->remove(p);
    }

    // "SD" or "FL" so DIRECTORY shows at a glance where programs really go.
    const char* volumeId() override { return core::files::usingSD() ? "SD" : "FL"; }

    uint32_t freeBytes() override {
        const uint64_t t = core::files::totalBytes(), u = core::files::usedBytes();
        return uint32_t(t > u ? (t - u) : 0);
    }

private:
    static void pathFor(const char* name, char* out, std::size_t n) {
        std::snprintf(out, n, "%s/%s.PRG", core::files::basicDir(), name);
    }
};

ProgramFiles s_programFiles;
}

void GhostBasic::onEnter() {
    core::keyboard::begin();          // idempotent; connection survives app switches

    screen_.reset();
    screen_.setBorder(ghost::COL_LTBLUE);
    screen_.setBackground(ghost::COL_BLUE);
    screen_.setTextColor(ghost::COL_LTBLUE);
    basic_.reset();
    basic_.setFiles(core::files::ready() ? &s_programFiles : nullptr);

    audioOk_ = board::audio::begin(SAMPLE_RATE);
    board::audio::setSpeakerEnable(false);   // only switched on while a voice sounds
    speakerOn_ = false;
    sid_.setSampleRate(SAMPLE_RATE);
    sid_.reset();
    basic_.setSid(&sid_);
    vic_.reset();
    basic_.setVic(&vic_);
    inputLine_.clear();

    // Boot banner with this machine's real memory figures.
    char line[Screen::COLS + 2];
    screen_.print("\n");
    screen_.print("        **** GHOST BASIC V1 ****\n");
    screen_.print("\n");
    std::snprintf(line, sizeof line, " %uMB PSRAM  %uMB FLASH\n",
                  unsigned(ESP.getPsramSize() / (1024 * 1024)),
                  unsigned(ESP.getFlashChipSize() / (1024 * 1024)));
    screen_.print(line);
    std::snprintf(line, sizeof line, " %u BASIC BYTES FREE\n",
                  unsigned(ESP.getFreeHeap()));
    screen_.print(line);
    screen_.print("\n");
    screen_.print("READY.\n");

    blinkT_   = 0;
    cursorOn_ = true;
}

void GhostBasic::onExit() {
    board::audio::setSpeakerEnable(false);
    speakerOn_ = false;
    sid_.reset();
    vic_.reset();
}

void GhostBasic::routeKey(uint8_t k) {
    if (k == 0x03) { basic_.breakRun(); return; }   // Esc -> RUN/STOP

    switch (basic_.mode()) {
    case Basic::Mode::Running:
        basic_.pushKey(k);            // available to GET
        break;

    case Basic::Mode::Input:
        if (k == 0x0D) {
            screen_.newLine();
            basic_.provideInput(inputLine_.c_str());
            inputLine_.clear();
        } else if (k == 0x14) {
            if (!inputLine_.empty()) { inputLine_.pop_back(); screen_.put(0x14); }
        } else if (k >= 0x20 && k < 0x80) {
            inputLine_ += char(k);
            screen_.put(k);
        }
        break;

    case Basic::Mode::Idle: {
        char line[2 * Screen::COLS + 1];   // a logical line can span two rows
        if (editor_.key(k, line, sizeof line)) basic_.execLine(line);
        break;
    }
    }
}

void GhostBasic::update(const core::Input& in, float dt) {
    if (in.backPressed) basic_.breakRun();

    basic_.setMillis(millis());   // drives the TI / TI$ jiffy clock

    uint8_t k;
    while (core::keyboard::next(k)) routeKey(k);

    basic_.poll();

    // Feed the speaker only while a voice is sounding: silence costs nothing
    // and the amplifier stays off, so an idle machine is genuinely quiet.
    if (audioOk_) {
        const bool want = sid_.active();
        if (want != speakerOn_) {
            board::audio::setSpeakerEnable(want);
            speakerOn_ = want;
        }
        if (want) {
            static int16_t buf[CHUNK_MAX];
            size_t n = size_t(dt * SAMPLE_RATE);
            if (n < 64) n = 64;
            if (n > CHUNK_MAX) n = CHUNK_MAX;
            sid_.render(buf, int(n));
            board::audio::play(buf, n);
        }
    }

    blinkT_ += dt;
    if (blinkT_ >= BLINK_PERIOD) {
        blinkT_ -= BLINK_PERIOD;
        cursorOn_ = !cursorOn_;
    }
}

void GhostBasic::render(board::gfx::Surface& s) {
    screen_.render(s, cursorOn_, &vic_, basic_.ram());
    if (vic_.active()) {
        // Collisions latch off the frame just drawn — its foreground mask is
        // fresh — so a PEEK of 53278/53279 reads what is on the glass now.
        vic_.updateCollisions(basic_.ram(), screen_.fgMask());
        screen_.renderSprites(s, vic_, basic_.ram());
    }
}

} // namespace apps
