# Ghost Pixel Lab — Developer Guide

Everything you need to build apps on this platform. The short version lives
in the [README](../README.md); this is the long one.

- [Architecture](#architecture)
- [The frame loop](#the-frame-loop)
- [Adding an app](#adding-an-app)
- [Engine toolbox](#engine-toolbox)
- [Cookbook](#cookbook)
  - [Drawing](#drawing)
  - [Sprites](#sprites)
  - [Touch patterns](#touch-patterns)
  - [Hardware buttons](#hardware-buttons)
  - [Tilt / IMU](#tilt--imu)
  - [Sounds & MP3](#sounds--mp3)
  - [Raw audio (synth & mic)](#raw-audio-synth--mic)
  - [Files & settings](#files--settings)
  - [Wi-Fi](#wi-fi)
  - [Bluetooth (BLE)](#bluetooth-ble)
- [Configuration](#configuration)
- [Rules of the road](#rules-of-the-road)
- [Performance](#performance)
- [Build, flash, assets](#build-flash-assets)

## Architecture

Three layers, strict direction of knowledge (top knows bottom, never the
other way around):

```
apps/    one file pair per experiment — only logic + drawing
core/    the engine: App interface, manager + menu, sound mixer, config
board/   the hardware: display, touch, imu, audio, power, rtc, storage, ...
```

- `src/main.cpp` is the composition root: bring up the board, register the
  apps, hand control to `core::manager`.
- Apps talk to hardware **only** through `board::*`. The `core::hw::*` flags
  say which peripherals answered at boot, so apps can degrade gracefully.
- [src/config.h](../src/config.h) holds the platform-wide tuning knobs.

## The frame loop

The firmware is one cooperative loop. Every frame the manager:

1. samples touch + buttons into a `core::Input`,
2. calls the active app's `update(input, dt)` — your logic,
3. calls `render(surface)` — your drawing into a RAM framebuffer,
4. hands the framebuffer to a background task that streams it to the panel
   over DMA **while you already draw the next frame**.

`dt` is the time since the previous frame in seconds. Your `update()` +
`render()` IS the frame: if you block, the UI hitches.

## Adding an app

`src/apps/myapp.h`:

```cpp
#pragma once
#include "core/app.h"

namespace apps {

class MyApp final : public core::App {
public:
    const char* name() const override { return "My App"; }    // menu title
    const char* info() const override { return "subtitle"; }  // menu subtitle

    void onEnter() override;                                  // optional
    void onExit()  override;                                  // optional
    void update(const core::Input& in, float dt) override;
    void render(board::gfx::Surface& s) override;

private:
    float x_ = 0;
};

} // namespace apps
```

`src/apps/myapp.cpp`:

```cpp
#include "myapp.h"
#include "board/display.h"

namespace apps {

void MyApp::onEnter() { x_ = 0; }
void MyApp::onExit()  {}

void MyApp::update(const core::Input& in, float dt) {
    x_ += 40.0f * dt;                       // 40 px/s, frame-rate independent
    if (in.justPressed) x_ = in.x;          // tap teleports
}

void MyApp::render(board::gfx::Surface& s) {
    s.clear(0x0000);                        // ALWAYS full redraw (see rules)
    s.filledCircle((int)x_, 224, 12, 0x07FF);
}

} // namespace apps
```

Register in `src/main.cpp`:

```cpp
#include "apps/myapp.h"
...
apps::MyApp myApp;          // next to the other app instances
...
core::manager::add(myApp);  // in setup()
```

Build, flash — the app is in the menu. Navigation (back via BOOT key or
top-swipe) comes for free.

## Engine toolbox

| Subsystem | API | What you get |
|---|---|---|
| Graphics | `board::gfx::Surface` | clear, px, h/vLine, line, thickLine, fillRect, circle, filledCircle, blit/blitKeyed (sprites), glyph/text/textWidth, `rgb(r,g,b)` |
| Display | `board::display` | 368×448 double-buffered, async DMA present, `setBrightness()` |
| Input | `core::Input` (per frame) | touch press/release edges, position, drag origin; BOOT/PWR key events |
| Motion | `board::imu` + `apps/tilt.h` | accel/gyro/temperature, screen-mapped gravity vector |
| Sound | `core::sound` | non-blocking tones, PCM clips, MP3 (any rate, auto-resampled), master volume — mixed by a background task |
| Raw audio | `board::audio` | 16 kHz mono PCM streaming out + mic recording in |
| Storage | `board::storage` | LittleFS (flash assets) + SD card as `fs::FS`; `loadToPsram()` |
| Power | `board::power` | battery %, voltage, VBUS, charging, PWR-key events |
| Clock | `board::rtc` | date/time read/write |
| Settings | `Preferences.h` (Arduino) | NVS key-value store, survives reboots |
| Wi-Fi | `WiFi.h` (Arduino) | full stack — see [Wi-Fi](#wi-fi) |
| BLE | `BLEDevice.h` (Arduino) | full Bluedroid stack — see [BLE](#bluetooth-ble) |
| System | `core::manager` | `fps()`, `frameStats()`; `core::hw::*` presence flags |

## Cookbook

### Drawing

Colors are RGB565. Build them with `Surface::rgb(r, g, b)` or use literals
(`0xF800` red, `0x07E0` green, `0x001F` blue, `0x07FF` cyan, `0xFFFF` white).

```cpp
void render(board::gfx::Surface& s) {
    s.clear(0x0000);
    s.fillRect(20, 40, 100, 60, Surface::rgb(40, 40, 60));
    s.line(0, 0, 367, 447, 0x07FF);
    s.thickLine(50, 300, 300, 320, 0xF800);   // 2 px wide
    s.circle(184, 224, 80, 0x8410);           // outline
    s.filledCircle(184, 224, 8, 0xFFFF);
    s.text(20, 12, "HELLO", 0x07FF, 3);       // 5x7 font, scale 3
    int w = s.textWidth("centered", 2);
    s.text((s.width - w) / 2, 60, "centered", 0xFFFF, 2);
}
```

For full-surface pixel effects, write `s.pixels[]` directly — but note the
buffer is stored in **panel byte order**: convert colors once with
`Surface::toPanel(color)` (see the Sand app for the pattern).

### Sprites

Sprites are plain `uint16_t` RGB565 arrays, row-major. Pick a key color for
transparency:

```cpp
static constexpr uint16_t SHIP[8 * 8] = { /* 0xF81F = transparent */ ... };

s.blit(x, y, image, w, h);                    // opaque copy
s.blitKeyed(x, y, SHIP, 8, 8, 0xF81F);        // keyed (sprite)
```

To convert a PNG: export raw RGB565 (e.g. with ImageMagick + a short script,
or any "image to RGB565 C array" web tool) and paste it as a `constexpr`
array in your app.

### Touch patterns

`core::Input` arrives every frame; FT3168 is single-touch.

```cpp
// Tap vs drag: decide on release, using total movement.
void update(const core::Input& in, float) {
    if (in.justPressed) { moved_ = 0; }
    if (in.pressed)     { moved_ += /* abs delta since last frame */; }
    if (in.justReleased && moved_ < 14) {
        // tap at (in.x, in.y); in.startX/startY = where the press began
    }
}
```

For scrollable lists, copy the pattern from `core/menu.cpp` or
`apps/music.cpp` (drag scrolls, small movement = tap, `tracking_` guard
against presses that began in another app).

### Hardware buttons

Two side keys. Default behavior: **BOOT** (upper) returns to the menu,
**PWR** (lower) cycles display brightness. Apps can take them over:

```cpp
class Game final : public core::App {
    bool capturesBackButton() const override { return true; }
    bool capturesPowerKey()   const override { return true; }

    void update(const core::Input& in, float dt) override {
        if (in.backPressed)    togglePause();   // BOOT, now yours
        if (in.keyPressed)     fireWeapon();    // PWR short press
        if (in.keyLongPressed) selfDestruct();  // PWR held ~1.5 s
    }
};
```

The top-edge swipe stays as a system escape hatch, so you can't lock
yourself in. Holding PWR ~6 s is a hardware power-off (PMU level).

### Tilt / IMU

`apps/tilt.h` maps the accelerometer into screen space (+x right, +y down).
If a direction is ever mirrored, fix the sign there once — all apps follow.

```cpp
#include "tilt.h"

float gx, gy;
if (apps::tilt::gravity(gx, gy)) {     // in g; false without an IMU
    vx_ += gx * 900.0f * dt;           // “roll a ball”
    vy_ += gy * 900.0f * dt;
}

board::imu::Vec3 gyro;
if (board::imu::readGyro(gyro)) { /* deg/s, chip frame */ }
```

For absolute orientation (complementary filter gyro+accel) see
`apps/cube3d.cpp`.

### Sounds & MP3

`core::sound` is the default way to make noise: it never blocks, mixes up to
4 voices + one music slot in a background task, and goes silent/asleep when
idle.

```cpp
#include "core/sound.h"

core::sound::tone(880.0f, 120);                      // beep, fire & forget
core::sound::tone(440.0f, 1000, 40);                 // freq, ms, volume 0-100

// PCM clip: 16 kHz mono int16, buffer must stay valid while playing
static constexpr int16_t PEW[1600] = { ... };
int v = core::sound::playClip(PEW, 1600);
core::sound::playClip(LOOP_PCM, n, 80, /*loop=*/true);
core::sound::stopClip(v);

// MP3 — any sample rate, mono or stereo (resampled to the speaker):
core::sound::playMp3File(*board::storage::flash(), "/track.mp3");
core::sound::playMp3File(*board::storage::sd(),    "/album/song.mp3");
core::sound::playMp3(flashArray, sizeof(flashArray), 100, /*loop=*/true);
core::sound::stopMp3();
bool on = core::sound::mp3Playing();

core::sound::setMasterVolume(50);                    // 0..100, perceptual
```

Getting MP3s onto the device: copy to a micro-SD card, or drop them in
`data/` and run `pio run -t uploadfs` (~3 MB LittleFS partition). Mono
64–96 kbps is plenty for this speaker. The Music app lists both sources.

The manager calls `core::sound::stopAll()` on every app switch — no app
inherits another's audio.

### Raw audio (synth & mic)

For synthesizers or microphone work, stream PCM yourself through
`board::audio` (16 kHz mono int16). Pace it by `dt`: read/write about
`dt * 16000` samples per frame (cap ~512); the DMA buffer holds ~90 ms.

```cpp
// Synth out (see apps/piano.cpp for the full pattern incl. envelope):
size_t n = min<size_t>(dt * 16000, 512);
static int16_t buf[512];
fillSamples(buf, n);
board::audio::play(buf, n);

// Mic in (see apps/echo.cpp, incl. draining stale DMA on record start):
size_t got = board::audio::record(buf, n);
```

Call `board::audio::begin()` in `onEnter()` (idempotent) and
`setSpeakerEnable(false)` in `onExit()`. Rule: an app uses **either**
`core::sound` **or** raw streaming, never both at once.

### Files & settings

```cpp
#include "board/storage.h"

// Both filesystems use the Arduino fs::FS API:
fs::FS* fl = board::storage::flash();   // LittleFS, nullptr if absent
fs::FS* sd = board::storage::sd();      // SD card,  nullptr if absent

File f = fl->open("/highscores.txt", "w");
f.printf("%d\n", score);
f.close();

// Whole file into PSRAM (free() it yourself):
size_t n;
uint8_t* data = board::storage::loadToPsram(*sd, "/level1.bin", n);

// Small persistent settings — NVS via Arduino Preferences:
#include <Preferences.h>
Preferences prefs;
prefs.begin("myapp");
int best = prefs.getInt("best", 0);
prefs.putInt("best", score);
prefs.end();
```

### Wi-Fi

Works alongside the display pipeline (framebuffers are in PSRAM, the radio
gets the internal heap it needs). Pattern: radio on in `onEnter()`, OFF in
`onExit()`, async calls only — see `apps/wifi_scan.cpp` for a complete app.

```cpp
#include <WiFi.h>

void onEnter() override {
    WiFi.mode(WIFI_STA);
    WiFi.begin("ssid", "pass");          // returns immediately
}
void update(const core::Input&, float) override {
    if (WiFi.status() == WL_CONNECTED) { /* render IP, do requests */ }
}
void onExit() override {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);                 // gives ~60 KB heap back
}
```

Blocking helpers (`HTTPClient::GET` etc.) stall frames — acceptable for a
one-shot fetch, but show a “loading” frame first, or move the request into
its own FreeRTOS task and poll a flag from `update()`.

### Bluetooth (BLE)

The Bluedroid stack ships with arduino-esp32 (`BLEDevice.h`): scanner,
client and server all work. Costs ~70 KB internal heap + noticeable init
time while on — same rule as Wi-Fi: init in `onEnter()`, `deinit` in
`onExit()`. For lighter builds consider the NimBLE-Arduino library
(`h2zero/NimBLE-Arduino` in `lib_deps`).

## Configuration

[src/config.h](../src/config.h) — compile-time switches, change → build →
flash:

| Knob | Meaning |
|---|---|
| `BOOT_BUTTON_HOME` | BOOT key returns to menu (apps can still capture it) |
| `SWIPE_HOME`, `SWIPE_EDGE_PX`, `SWIPE_DIST_PX` | top-edge escape gesture + geometry |
| `PWR_KEY_BRIGHTNESS`, `BRIGHTNESS_STEPS[]` | PWR-key brightness cycling |
| `DEFAULT_VOLUME` | master volume at boot (0–100) |
| `STATS_LOG_MS` | serial fps/frame log interval, 0 = off |

## Rules of the road

- **`render()` must redraw everything, every frame** (start with
  `s.clear()`). The display double-buffers; leftover pixels are two frames
  old.
- **Never block in `update()`.** No `delay()`, no `audio::beep()`, no
  unbounded loops. One slow frame = visible hitch.
- **Slow I2C belongs behind a timer.** Battery and RTC reads cost ~0.5 ms —
  cache them at 2 Hz like Sensor Lab does. IMU per frame is fine; touch is
  already sampled for you.
- **Sound: engine first** (`core::sound`); raw `board::audio` streaming only
  for synth/mic apps; never both in one app.
- **Big buffers go to PSRAM** (`heap_caps_malloc(n, MALLOC_CAP_SPIRAM)` in
  `onEnter()`, `free()` in `onExit()`). ~8 MB there; internal heap is
  precious (Wi-Fi/BLE need it).
- **Tilt goes through `apps/tilt.h`** so the axis mapping lives in one place.
- **Touch is single-point** — design for taps, drags, holds.
- **The top edge belongs to the system** (escape swipe) — no critical
  controls in the top 40 px.

## Performance

What the pipeline does for you:

- Two PSRAM framebuffers; `present()` hands the finished frame to a
  presenter task (core 0) that streams it via queued DMA while the app draws
  the next frame on core 1. Frame time ≈ max(draw, transfer), not the sum.
- Framebuffers are stored in panel byte order, so no per-frame byte-swap
  pass; QSPI runs at the panel's rated 80 MHz (full-frame wire time ~8 ms →
  hard cap ≈ 100 fps; realistic full-redraw apps land at 50–70 fps).
- `clear()` is a `memset` when the color allows; `fillRect` uses 32-bit
  stores; glyphs have a fast path — text is cheap.

What you watch: `core::manager::frameStats()` gives update/draw/show ms per
frame (also on serial every 2 s and live in Sensor Lab). `draw` is your
budget; `show` near 0 means the transfer is fully hidden. If `draw` grows,
reduce overdraw (don't paint the same pixels twice) before micro-optimizing.

## Build, flash, assets

Requires [PlatformIO](https://platformio.org/).

```sh
pio run -t upload      # build + flash firmware
pio run -t uploadfs    # pack data/ into LittleFS and flash it (assets, mp3)
pio device monitor     # serial @ 115200  (boot log, fps stats, [audio] ...)
```

Partition table is `default_16MB.csv`: 6.5 MB app, ~3 MB LittleFS. The first
flash after switching layouts reformats the data partition. The serial boot
log (`[boot] imu=1 touch=1 ...`) tells you which peripherals answered —
the same flags apps see as `core::hw::*`.
