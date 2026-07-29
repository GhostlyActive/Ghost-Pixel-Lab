# Ghost BASIC — host test suite

289 assertions covering the machine, not just the language:

- **Interpreter** — operator precedence, `^` associativity, numeric and string
  functions at their boundaries, comparisons and logic, control flow, nested
  loops, arrays, `DATA`/`READ`/`RESTORE`, `DEF FN`, `ON…GOTO/GOSUB`, integer
  variables, two-character name significance, and how numbers come out of
  `PRINT`.
- **Editor and screen** — cursor movement at the edges, the wrap boundary,
  scrolling, and the signature move: cursor up to an old line, change it, press
  RETURN and it is re-entered.
- **Storage** — `SAVE`/`LOAD`/`SCRATCH` against an in-memory disk, a `DIRECTORY`
  that looks like a 1541 listing, and file I/O via
  `OPEN`/`CLOSE`/`PRINT#`/`INPUT#`/`GET#`/`CMD`.
- **The chips** — the SID including its filter, and the VIC-II: sprites with
  priority and both collision registers, custom character sets, and both bitmap
  modes.
- **Robustness** — every error message the machine can print, malformed input
  that must not wedge it, and an endless loop that must stay interruptible.

```sh
./run.sh
```

```
===== 289 passed, 0 failed =====
```

## Why this works off-device

`src/apps/ghost_basic/` deliberately depends on nothing from `core::` or Arduino:

- `screen.cpp` owns the 40×25 character matrix and only touches the display
  through `board::gfx::Surface` in `render()`.
- `basic.cpp` reaches storage through the abstract `ghost::Files` interface, so
  the suite substitutes an in-memory "disk".

`../hostshim/board/` provides ~90 lines of stand-ins for `Surface` and the
display dimensions, shared with the Outer Pixels suite. That is the entire
porting layer — everything else is the real firmware code, not a copy, so a
green run says something about what actually ships.

What it does *not* cover is the hardware underneath: no SPI, PSRAM, DMA or BLE.
Bugs that live there are invisible here and need the device.

Every bug this suite has caught so far was invisible on the device: a 40-column
line silently losing its `RETURN`, `LEFT$` being parsed as an array, `-2^2`
evaluating to `4` instead of `-4`.
