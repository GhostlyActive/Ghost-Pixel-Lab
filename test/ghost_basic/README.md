# Ghost BASIC — host test suite

99 assertions covering the BASIC interpreter: operator precedence, string and
numeric functions, control flow, arrays, `DATA`/`READ`, `DEF FN`, `INPUT`,
`SAVE`/`LOAD`/`DIRECTORY`, the C64 error messages, and `PRINT` formatting.

```sh
./run.sh
```

```
===== 99 passed, 0 failed =====
```

## Why this works off-device

`src/apps/ghost_basic/` deliberately depends on nothing from `core::` or Arduino:

- `screen.cpp` owns the 40×25 character matrix and only touches the display
  through `board::gfx::Surface` in `render()`.
- `basic.cpp` reaches storage through the abstract `ghost::Files` interface, so
  the suite substitutes an in-memory "disk".

`hostshim/board/` provides ~40 lines of stand-ins for `Surface` and the display
dimensions. That is the entire porting layer — everything else is the real
firmware code, not a copy, so a green run says something about what actually
ships.

Every bug this suite has caught so far was invisible on the device: a 40-column
line silently losing its `RETURN`, `LEFT$` being parsed as an array, `-2^2`
evaluating to `4` instead of `-4`.
