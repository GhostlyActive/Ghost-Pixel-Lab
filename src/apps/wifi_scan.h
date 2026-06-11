// WiFi Scan: async network scan, list with RSSI bars. Doubles as proof that
// the radio runs alongside the display pipeline; the radio is switched off
// again on exit to give the heap back.
#pragma once

#include "core/app.h"
#include <cstdint>

namespace apps {

class WifiScan final : public core::App {
public:
    const char* name() const override { return "WiFi Scan"; }
    const char* info() const override { return "networks around you"; }

    void onEnter() override;
    void onExit() override;
    void update(const core::Input& in, float dt) override;
    void render(board::gfx::Surface& s) override;

private:
    static constexpr int MAX_NETS = 14;

    struct Net {
        char    ssid[26];
        int16_t rssi;
        uint8_t channel;
        bool    open;
    };

    Net   nets_[MAX_NETS];
    int   count_    = 0;
    bool  scanning_ = false;
    float rescan_   = 0;
};

} // namespace apps
