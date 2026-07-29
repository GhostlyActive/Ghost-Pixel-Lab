# Outer Pixels — host test suite

33 assertions covering the simulation and both renderers:

- **Orbits** — parents are listed before their children (the single forward pass
  in `update()` depends on it), planets hold their orbital radius, moons follow
  their planet rather than the sun, and position depends only on `t`.
  `bodyVelocity()` is checked against a finite difference of the positions it is
  supposed to describe.
- **Terrain** — the tile has real relief, sampling wraps in both directions so
  flying never reaches an edge, and two planets build different tiles.
- **Flight model** — steering keeps the body frame orthonormal, gravity pulls
  toward a planet, and touchdown is judged *relative to the planet*: matching a
  moon's orbital velocity lands, hitting it at speed bounces. The sun and the
  comets are never landable.
- **Renderers** — both paint the frame at the real panel dimensions, orbit lines
  add ink rather than replacing it, the cloud dive starts thick and thins out,
  and the surface camera never sinks below the ground.

```sh
./run.sh
```

```
===== 33 passed, 0 failed =====
```

## Why this works off-device

`src/apps/outer_pixels/` deliberately depends on nothing from `core::` or
Arduino. The only thing it needs from the device is `board::gfx::Surface`, which
`../hostshim/` stands in for — so the orbits, the flight model, the terrain
generator and both renderers all compile straight on a PC.

That is also what the suite is really guarding: `src/apps/outer_pixels.cpp` is
the `core::App` shell and keeps everything device-shaped (FreeRTOS, PSRAM, input
folding). If any of that leaks back into the folder, this suite stops compiling
— a broken split shows up in a second instead of after a flash.

What it does *not* cover is the hardware underneath: no SPI, PSRAM, DMA or BLE.
The one bug that has actually stopped this app on the device — the display's
per-transfer DMA allocation failing on a heap that BLE and audio had already
fragmented — lives entirely in that gap and needs the real board to see.
