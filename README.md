# Ghost Pixel Lab

<div align="center">
  <table>
    <tr>
      <td><img src="Media/outer_pixel_video.gif"   alt="Outer Pixels — space"   width="290"></td>
      <td><img src="Media/outer_pixel_video_2.gif" alt="Outer Pixels — surface" width="290"></td>
    </tr>
    <tr>
      <td><img src="Media/outer_pixel_1.jpg" alt="Outer Pixels screenshot 1" width="290"></td>
      <td><img src="Media/outer_pixel_2.jpg" alt="Outer Pixels screenshot 2" width="290"></td>
    </tr>
  </table>
</div>

<p align="center">
  <a href="https://www.youtube.com/watch?v=ePTqaHAo_ig">▶ Watch on YouTube</a>
</p>

Pocket AMOLED playground for the Waveshare ESP32-S3-Touch-AMOLED-1.8: a tiny
app engine (touch launcher, double-buffered DMA renderer, sound mixer with
MP3, storage, Wi-Fi/BLE) plus a growing pile of experiments. Modern C++.

## Hardware

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

## Apps

Boots into a touch launcher; every experiment is one small app class.
Currently on board: **Outer Pixels** (Outer-Wilds-style space sim — fly between
orbiting planets and drop onto a voxel surface), **Cube 3D** (IMU wireframe),
**Sensor Lab** (hardware dashboard), **Echo** (mic → speaker), **Piano** (touch
synth), **Sand** (falling sand + tilt), **Maze Ball**, **Level** (spirit
level), **Music** (MP3 from flash & SD), **WiFi Scan**, **Pad Lab** (Xbox
controller over BLE, incl. rumble).

Tap to launch — BOOT key or a swipe from the top edge goes back, the PWR key
cycles brightness.

## Outer Pixels

The headline app: an Outer-Wilds-style space sim (no 3D engine). Fly a 6-DOF
ship between orbiting planets with the Xbox pad; drop low and it switches to a
Comanche-style **voxel terrain** you fly through — procedural, generated in the
background so it's seamless.

## Build & flash

Requires [PlatformIO](https://platformio.org/).

```sh
pio run -t upload      # build + flash
pio run -t uploadfs    # flash the data/ folder (assets, mp3s)
pio device monitor     # serial @ 115200
```

## Layout

```
src/
  main.cpp      # composition root: board init + app registration
  core/         # engine: App interface, manager, menu, sound mixer
  apps/         # one file pair per experiment
  board/        # hardware modules (display, imu, touch, audio, storage, ...)
data/           # assets packed into LittleFS
docs/           # developer guide
```

## Documentation

How to write an app, the engine APIs, code examples for sound/MP3, sprites,
storage, Wi-Fi/BLE, buttons, configuration and performance — all in the
**[Developer Guide](docs/GUIDE.md)**.

## Status

Sandbox repo. Things change, demos get replaced, APIs are not stable.
