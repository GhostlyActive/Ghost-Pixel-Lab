#include "wifi_scan.h"
#include "board/display.h"

#include <WiFi.h>
#include <cstdio>
#include <cstring>

namespace apps {

namespace {

using board::gfx::Surface;

constexpr int MARGIN   = 14;
constexpr int LIST_TOP = 88;
constexpr int ROW_H    = 50;

constexpr uint16_t COL_ACCENT = 0x07FF;
constexpr uint16_t COL_TEXT   = 0xFFFF;
constexpr uint16_t COL_DIM    = 0x8410;
constexpr uint16_t COL_ROW    = 0x10A2;

} // namespace

void WifiScan::onEnter() {
    count_    = 0;
    rescan_   = 0;
    WiFi.mode(WIFI_STA);
    WiFi.scanNetworks(true /*async*/, true /*show hidden*/);
    scanning_ = true;
}

void WifiScan::onExit() {
    WiFi.scanDelete();
    WiFi.mode(WIFI_OFF);  // give the internal heap back
}

void WifiScan::update(const core::Input&, float dt) {
    if (scanning_) {
        const int n = WiFi.scanComplete();
        if (n >= 0) {
            count_ = 0;
            for (int i = 0; i < n && count_ < MAX_NETS; ++i) {
                Net& d = nets_[count_++];
                const String ssid = WiFi.SSID(i);
                snprintf(d.ssid, sizeof(d.ssid), "%s",
                         ssid.length() ? ssid.c_str() : "<hidden>");
                d.rssi    = static_cast<int16_t>(WiFi.RSSI(i));
                d.channel = static_cast<uint8_t>(WiFi.channel(i));
                d.open    = WiFi.encryptionType(i) == WIFI_AUTH_OPEN;
            }
            // results arrive sorted by RSSI already; keep as-is
            WiFi.scanDelete();
            scanning_ = false;
            rescan_   = 0;
        }
    } else {
        rescan_ += dt;
        if (rescan_ > 6.0f) {
            WiFi.scanNetworks(true, true);
            scanning_ = true;
        }
    }
}

void WifiScan::render(Surface& s) {
    const int W = board::display::WIDTH;
    char line[40];

    s.clear(0x0000);
    s.text((W - s.textWidth("WIFI SCAN", 3)) / 2, 14, "WIFI SCAN", COL_ACCENT, 3);

    if (scanning_ && count_ == 0) {
        s.text((W - s.textWidth("scanning...", 2)) / 2, 52, "scanning...", COL_DIM, 2);
    } else {
        snprintf(line, sizeof(line), "%d networks%s", count_,
                 scanning_ ? "  (rescan...)" : "");
        s.text((W - s.textWidth(line, 2)) / 2, 52, line, COL_DIM, 2);
    }

    for (int i = 0; i < count_; ++i) {
        const int y = LIST_TOP + i * ROW_H;
        if (y + ROW_H > board::display::HEIGHT) break;
        s.fillRect(MARGIN, y, W - 2 * MARGIN, ROW_H - 6, COL_ROW);
        s.text(MARGIN + 10, y + 6, nets_[i].ssid, COL_TEXT, 2);
        snprintf(line, sizeof(line), "ch%-2d  %s  %d dBm", nets_[i].channel,
                 nets_[i].open ? "open" : "lock", nets_[i].rssi);
        s.text(MARGIN + 10, y + 28, line, COL_DIM, 1);

        // RSSI bars: -90..-40 dBm -> 0..4.
        int bars = (nets_[i].rssi + 90) / 13;
        if (bars < 0) bars = 0;
        if (bars > 4) bars = 4;
        for (int b = 0; b < 4; ++b) {
            const int bh = 6 + b * 6;
            const uint16_t c = b < bars ? COL_ACCENT : 0x2945;
            s.fillRect(W - MARGIN - 52 + b * 11, y + 36 - bh, 8, bh, c);
        }
    }
}

} // namespace apps
