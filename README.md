# Ghost Pixel Lab

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

All peripherals live behind `board::*` modules in [src/board/](src/board/), so demos in `main.cpp` only deal with framebuffers and logic.

## Current demo

[src/main.cpp](src/main.cpp) — wireframe 3D cube with a scrolling marquee, orientation driven by the IMU through a complementary filter. Tilt the board, the cube tilts with it.

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
  main.cpp      # current experiment
  board/        # hardware modules (display, imu, audio, power, ...)
include/        # shared headers
lib/            # project-local libraries
```

## Status

Sandbox repo. Things change, demos get replaced, APIs are not stable.
