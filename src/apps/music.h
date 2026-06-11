// Music: lists .mp3 files from internal flash (LittleFS) and the SD card,
// tap to play through the sound engine, master volume control.
#pragma once

#include "core/app.h"
#include "config.h"
#include <cstdint>

namespace apps {

class Music final : public core::App {
public:
    const char* name() const override { return "Music"; }
    const char* info() const override { return "MP3 from flash & SD"; }

    void onEnter() override;
    void update(const core::Input& in, float dt) override;
    void render(board::gfx::Surface& s) override;

private:
    static constexpr int MAX_FILES = 24;

    struct Entry {
        char    path[56];
        char    label[24];
        uint8_t src;  // 0 = flash, 1 = sd
    };

    void scanDir(uint8_t src);
    void tapRow(int idx);

    Entry   files_[MAX_FILES];
    int     count_      = 0;
    int     playingIdx_ = -1;
    uint8_t volume_     = config::DEFAULT_VOLUME;
    bool    soundOk_    = false;

    // list touch state
    float   scrollY_  = 0;
    int     pressIdx_ = -1;
    int     moved_    = 0;
    int16_t lastY_    = 0;
    bool    tracking_ = false;
};

} // namespace apps
