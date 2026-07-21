// Bluetooth LE keyboard host (HID over GATT).
//
// begin() starts a background task. If a keyboard was paired earlier its
// address is loaded from storage (SD card preferred, internal flash as
// fallback) and reconnected automatically on every boot — no scanning, no
// re-pairing. If nothing is stored yet, use the BLE Scan app: it drives the
// discovery API below, and pairWith() remembers the chosen device.
//
// The ESP32-S3 is BLE-only, so this works with BLE keyboards (e.g. Keychron
// K2 Pro in Bluetooth mode) — not with Bluetooth-Classic keyboards.
//
// Apps drain decoded keys once per frame with next(). Keys are PETSCII-ish:
// printable ASCII, RETURN (0x0D), DELETE (0x14), the cursor codes
// (0x11/0x91/0x1D/0x9D), HOME (0x13) and STOP (0x03, from Esc).
#pragma once

#include <cstdint>

namespace core::keyboard {

bool begin();                    // start BLE + connection task (idempotent)
[[nodiscard]] bool connected();

// Pop one decoded key from the buffer. Returns false when empty.
//
// This also drains the USB serial port, so the keyboard of a connected
// computer works as an input device (`pio device monitor` and type). Handy
// while no BLE keyboard is paired — and note that Bluetooth-Classic-only
// keyboards can never work here, the ESP32-S3 has no Classic radio at all.
// Ctrl+C acts as RUN/STOP; the arrow keys are understood.
bool next(uint8_t& petscii);

// ---------------------------------------------------------------------------
// Discovery / pairing — used by the BLE Scan app.
// ---------------------------------------------------------------------------

struct Device {
    char     name[26];    // advertised name, or "" — many devices send none
    char     addr[20];    // "aa:bb:cc:dd:ee:ff"
    uint8_t  type;        // BLE address type — needed to reconnect later
    int8_t   rssi;        // signal strength: the nearest device is the biggest
    uint16_t appearance;  // 0x03C1 == keyboard, when the device bothers to say
    bool     hid;         // advertises the HID service or a keyboard appearance
    bool     connectable; // beacons and trackers are not; a keyboard is
};

// Tapping a device probes it: connect, verify it really speaks keyboard HID,
// read its true name off the device, and only then remember it.
enum class PairState { Idle, Connecting, Paired, NotAKeyboard };

void startScan();                    // list *every* BLE device nearby
void stopScan();
[[nodiscard]] bool scanning();

int    deviceCount();   // entries kept in the list
int    seenCount();     // unique devices seen in total (may exceed the list)
Device device(int i);

// Probe device i and, if it is a keyboard, remember it. Survives reboots.
// Watch pairState() for the outcome.
void pairWith(int i);
[[nodiscard]] PairState pairState();

const char* savedName();             // "" when nothing is paired yet
const char* savedAddr();             // "" when nothing is paired yet
void        forget();                // drop the stored pairing

} // namespace core::keyboard
