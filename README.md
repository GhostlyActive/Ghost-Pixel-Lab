# Ghost Pixel Lab

<p align="center">
  <img src="Media/wireframe_cube.gif" alt="Wireframe cube demo" width="320" height="240">
</p>

Pocket AMOLED playground for the Waveshare ESP32-S3-Touch-AMOLED-1.8 —
3D rendering, particle / physics demos, audio experiments. Modern C++.

## Hardware

The board:

| Part              | Role                                                       |
|-------------------|------------------------------------------------------------|
| ESP32-S3R8        | 240 MHz, 8 MB octal PSRAM, 16 MB flash, Wi-Fi 4 + BLE 5    |
| SH8601            | 1.8" AMOLED, 368 × 448, QSPI                               |
| FT3168            | capacitive touch                                           |
| QMI8658           | 6-axis IMU (gyro + accelerometer)                          |
| ES8311            | mono audio codec + on-board speaker + MEMS microphone      |
| AXP2101           | power management, LiPo charging + battery telemetry        |
| PCF85063          | RTC with backup-battery pads                               |
| TCA9554           | 8-bit I/O expander (drives LCD / TP reset internally)      |

Product page: <https://www.waveshare.com/esp32-s3-touch-amoled-1.8.htm>

All peripherals live behind `board::*` modules in [src/board/](src/board/), so apps only deal with framebuffers and logic.

## Apps & menu

The firmware boots into a touch-driven launcher menu. Each experiment is an
app: a small class implementing `core::App` ([src/core/app.h](src/core/app.h))
that gets touch input and a framebuffer `Surface` once per frame. The app
manager ([src/core/app_manager.h](src/core/app_manager.h)) runs exactly one
app at a time and handles app switching, input sampling and FPS tracking.

Back to the menu from any app: **swipe down from the top edge** or press the
**BOOT button**.

Current apps:

- **Cube 3D** ([src/apps/cube3d.cpp](src/apps/cube3d.cpp)) — wireframe cube
  with a scrolling marquee, orientation driven by the IMU through a
  complementary filter. Tilt the board, the cube tilts with it.
- **Sensor Lab** ([src/apps/sensor_lab.cpp](src/apps/sensor_lab.cpp)) — live
  dashboard for all on-board hardware: IMU, battery/VBUS, RTC, touch
  (with crosshair), FPS, heap and PSRAM.
- **Echo** ([src/apps/echo.cpp](src/apps/echo.cpp)) — hold to record from the
  mic (up to 8 s into PSRAM), release to play it back. Waveform + level meter.
- **Piano** ([src/apps/piano.cpp](src/apps/piano.cpp)) — one touch octave with
  black keys, monophonic sine synth with attack/release envelope.
- **Sand** ([src/apps/sand.cpp](src/apps/sand.cpp)) — falling-sand automaton,
  touch pours sand, tilting the board redirects gravity.
- **Maze Ball** ([src/apps/maze.cpp](src/apps/maze.cpp)) — roll the ball
  through a generated maze by tilting; new maze every goal.
- **Level** ([src/apps/level.cpp](src/apps/level.cpp)) — spirit level: bubble,
  degree rings and numeric tilt readout from the accelerometer.

Tilt-driven apps share the accel-to-screen mapping in
[src/apps/tilt.h](src/apps/tilt.h) — if a direction is mirrored on your
board, flip the signs there once.

### Adding an app

1. Create `src/apps/myapp.h` / `.cpp` with a class implementing `core::App`:

   ```cpp
   class MyApp final : public core::App {
       const char* name() const override { return "My App"; }
       const char* info() const override { return "what it does"; }
       void update(const core::Input& in, float dt) override { /* logic */ }
       void render(board::gfx::Surface& s) override { /* draw   */ }
   };
   ```

2. Instantiate it in [src/main.cpp](src/main.cpp) and register it with
   `core::manager::add(myApp);` — it appears in the menu, navigation is free.

Apps talk to hardware directly through `board::*`; the `core::hw::*` flags
say which peripherals answered at boot, so apps can degrade gracefully.

### Rules of the road (read before writing an app)

The framework is a cooperative single loop — an app's `update()` + `render()`
IS the frame. The rules that follow from that:

- **`render()` must redraw everything, every frame** (start with `s.clear()`).
  The display double-buffers; leftover pixels are two frames old.
- **Never block in `update()`.** No `delay()`, no `audio::beep()` (it busy-
  streams the whole tone), no unbounded loops. One slow frame = visible hitch.
- **Slow I2C belongs behind a timer.** Battery and RTC reads cost ~0.5 ms
  each — cache them at 2 Hz like Sensor Lab does. IMU/touch per frame is fine
  (touch is already sampled for you and arrives in `Input`).
- **Audio is dt-paced streaming:** per frame, read/write about
  `dt * sampleRate` samples (cap ~512) — see Echo and Piano. The DMA buffer
  holds ~90 ms; underruns play silence, not garbage. `audio::begin()` is
  idempotent; call it in `onEnter()`, and `setSpeakerEnable(false)` in
  `onExit()`.
- **Big buffers go to PSRAM:** `heap_caps_malloc(n, MALLOC_CAP_SPIRAM)` in
  `onEnter()`, `free()` in `onExit()`. ~8 MB available; internal heap is
  precious (Wi-Fi/BLE need it later).
- **Tilt input goes through [src/apps/tilt.h](src/apps/tilt.h)** so the
  accel-to-screen mapping lives in one place.
- **Touch is single-point** (FT3168 driver reads one finger). Design UIs
  around taps, drags and holds — no pinch, no chords.
- **Drawing toolbox** (`board::gfx::Surface`): `clear`, `px`, `hLine`,
  `vLine`, `line`, `thickLine`, `fillRect`, `circle`, `filledCircle`,
  `glyph`, `text`, `textWidth`. Direct `pixels[]` access is allowed for
  full-surface effects (see Sand) — remember `Surface::toPanel()` byte order.
- **The top edge belongs to the system:** a swipe down from the top 40 px
  returns to the menu. Don't put critical controls there.

## Build & flash

Requires [PlatformIO](https://platformio.org/).

```sh
pio run -t upload      # build + flash
pio device monitor     # serial @ 115200
```

Board target and flags are in [platformio.ini](platformio.ini).

## Layout

```
src/
  main.cpp      # composition root: board init + app registration
  core/         # app framework: App interface, manager, launcher menu
  apps/         # one file pair per experiment (cube3d, sensor_lab, ...)
  board/        # hardware modules (display, imu, touch, audio, power, ...)
include/        # shared headers
lib/            # project-local libraries
```

## Performance notes

- **Double-buffered async display pipeline:** two framebuffers (368 × 448 ×
  RGB565 ≈ 322 KB each) live in PSRAM. `present()` flips buffers and hands
  the finished frame to a presenter task on core 0, which streams it to the
  panel with queued DMA transactions while the app already draws the next
  frame on core 1. Frame time ≈ max(draw, transfer) instead of the sum.
- Framebuffers are stored **in panel byte order** (big-endian RGB565):
  primitives swap their color once on entry, so the transfer needs no
  per-frame CPU byte-swap pass over the 322 KB.
- QSPI runs at the panel's rated 80 MHz; pure wire time for a full frame is
  ~8 ms, which caps a full-frame redraw at ~100 FPS.
- Apps must redraw the whole surface every frame (start `render()` with
  `s.clear()`) — leftovers in the buffer are two frames old.
- Drawing happens entirely in RAM via `board::gfx::Surface` — no per-pixel
  panel I/O. `clear()` uses `memset` when the color allows it.
- The manager samples touch once per frame; slow I2C reads (battery, RTC)
  are cached at 2 Hz in the apps that show them.
- `core::manager::fps()` and `core::manager::frameStats()` (update / draw /
  show ms per frame; "show" = time blocked on the previous transfer) are
  available to every app, logged to serial every 2 s and shown in Sensor Lab.

## Status

Sandbox repo. Things change, demos get replaced, APIs are not stable.
