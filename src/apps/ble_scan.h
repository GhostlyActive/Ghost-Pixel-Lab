// BLE Scan: lists every Bluetooth-LE device nearby and lets you pick the one
// that is your keyboard. The choice is written to the SD card (or internal
// flash if no card), so core::keyboard reconnects to it automatically on every
// boot — no scanning, no re-pairing.
//
// Deliberately unfiltered: many keyboards do not advertise the HID service or
// an appearance, so a "keyboards only" list can come up empty. Devices that do
// look like a keyboard are marked HID and sorted to the top.
#pragma once

#include "core/app.h"
#include "core/keyboard.h"

#include <cstdint>

namespace apps {

class BleScan final : public core::App {
public:
    const char* name() const override { return "BLE Scan"; }
    const char* info() const override { return "pair a keyboard"; }

    void onEnter() override;
    void onExit() override;
    void update(const core::Input& in, float dt) override;
    void render(board::gfx::Surface& s) override;

private:
    static constexpr int MAX_ROWS = 32;

    int rowAt(int y) const;

    core::keyboard::Device rows_[MAX_ROWS];
    int     count_    = 0;
    float   refresh_  = 0;
    int     pressIdx_ = -1;
    int     moved_    = 0;
    int16_t lastY_    = 0;
    bool    tracking_ = false;
    bool    onButton_ = false;         // press started on the filter toggle
    bool    onForget_ = false;         // press started on the forget button
    bool    onRescan_ = false;         // press started on the rescan button
    float   scrollY_  = 0;
    int     pairedIdx_ = -1;           // row the user tapped, for feedback
    bool    onlyConnectable_ = false;  // show everything: a filter that hides
                                       // the one device you need is worse than noise
};

} // namespace apps
