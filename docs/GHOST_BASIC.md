# Ghost BASIC — Manual

How to write small applications in BASIC on the Ghost Pixel Lab. This manual
describes exactly the language scope that is currently built in — nothing that
isn't possible yet is promised here.

---

## 1. Input

There is no on-screen keyboard. Typing is done in two ways.

### Via the serial console (works immediately, no accessories needed)

```sh
pio device monitor
```

Open Ghost BASIC and just start typing — the characters land directly on the
device's screen. `Ctrl+C` acts as RUN/STOP.

### Via a Bluetooth keyboard

1. Open **BLE Scan** under *Others*.
2. Put the keyboard into pairing mode and hold it near the display — the
   strongest entry sorts to the top.
3. Tap it. The device connects, verifies it really is a keyboard, and remembers
   it. From then on it reconnects automatically on every start, without a scan.

> **The ESP32-S3 has no Bluetooth-Classic radio, only BLE.** Keyboards that
> pair over Classic are invisible to this device and cannot work — the Keychron
> K2 Pro, for example, does not. That a phone finds it proves nothing: a phone's
> Bluetooth settings search for both. The only reliable test is: **what shows up
> in the BLE Scan list works.** That makes it your pre-purchase test too.

### Special keys

| Key | Effect |
|-----|--------|
| `RETURN` | Enter / submit the line |
| `Backspace` | Delete the character to the left |
| Arrow keys | Move the cursor |
| `Esc` / `Ctrl+C` / BOOT button | **RUN/STOP** — aborts a running program |
| `Home` | Move the cursor to the top left |

The character set is uppercase (just like a powered-on C64). Whatever you type
appears in uppercase.

---

## 2. The two modes

**Direct mode** — type a command *without* a line number, press `RETURN`, and it
executes immediately:

```
PRINT 2+2
 4
READY.
```

**Program mode** — prefix a line number. The line is not executed but stored:

```basic
10 PRINT "HELLO"
20 GOTO 10
RUN
```

- `RUN` starts the program.
- `LIST` displays it (`LIST 20` for one line, `LIST 10-30` for a range).
- `NEW` deletes the entire program.
- A line number on its own (`20` + `RETURN`) deletes that line.
- Entering the same number again replaces the line.

---

## 3. The editor — "the screen is the input"

There is no dedicated input field. You type directly onto the screen. `RETURN`
re-reads **the whole line the cursor is currently on**. That means:

- Move up to an old line with the arrow keys, change something, press `RETURN` —
  the line is entered anew.
- A line can be up to **80 characters** long (it visually wraps to two screen
  rows but counts as one).

---

## 4. Language reference

### Variables

- **Numbers:** `A`, `X`, `NUMBER` — floating point.
- **Strings:** end with `$` — `A$`, `NAME$`.
- Only the **first two characters** are significant: `COUNT` and `COUNTER` are
  the same variable.

```basic
10 A = 5
20 B$ = "TEXT"
30 PRINT A, B$
```

### Expressions

- Math: `+  -  *  /  ^` (power), parentheses, signs.
- Comparisons: `=  <>  <  >  <=  >=` — return **-1** (true) or **0** (false).
- Logic: `AND  OR  NOT`.
- Concatenate strings with `+`: `"AB" + "CD"` gives `"ABCD"`.

### PRINT

- `;` appends directly, `,` jumps to the next column (grid of 10).
- Numbers get a leading space (placeholder for the sign).
- `TAB(n)` jumps to column *n*, `SPC(n)` prints *n* spaces.
- A `;` at the end of a line suppresses the line break.

```basic
PRINT "X ="; X
PRINT "A", "B", "C"
PRINT TAB(10); "INDENTED"
```

### Branches and loops

```basic
10 IF A > 10 THEN PRINT "LARGE"
20 IF A = 0 THEN 100          : REM = GOTO 100
30 FOR I = 1 TO 10 STEP 2
40   PRINT I
50 NEXT I
60 ON X GOTO 100, 200, 300    : REM jumps by X (1,2,3)
```

- `IF … THEN` — if true the rest of the line runs, if false it is skipped.
  `THEN 100` or `IF … GOTO 100` jump to a line.
- `FOR … NEXT` with optional `STEP` (may be negative).
- `GOSUB 500` … `RETURN` for subroutines.
- `ON n GOTO/GOSUB list` — selects the *n*-th destination.

### Input

```basic
10 INPUT "WHAT IS YOUR NAME"; N$
20 PRINT "HELLO "; N$
30 INPUT "TWO NUMBERS"; A, B
```

- `INPUT` waits until you type a line and press `RETURN`. Multiple variables are
  read separated by commas.
- `GET A$` reads **one** key without waiting (empty string if nothing is
  pressed). The typical wait loop:

```basic
10 GET A$ : IF A$ = "" THEN 10
20 PRINT "YOU PRESSED '"; A$; "'"
```

### Arrays

```basic
10 DIM P(5)          : REM numbers 0..5
20 DIM N$(10)        : REM strings 0..10
30 DIM M(3, 3)       : REM two-dimensional
```

Arrays used without `DIM` are created automatically with 0..10 per dimension.

### DATA / READ

```basic
10 FOR I = 1 TO 3 : READ W$(I) : NEXT
20 DATA "RED", "GREEN", "BLUE"
30 RESTORE            : REM reset the data pointer
```

`READ` fetches values from the `DATA` lines in sequence; `RESTORE` starts over.

### Custom functions

```basic
10 DEF FN K(X) = X * X * X
20 PRINT FN K(3)      : REM 27
```

### Memory: POKE / PEEK

`POKE` and `PEEK` are a real address bus, at the same addresses as the original:

| Address | Meaning |
|---------|---------|
| `1024`–`2023` | Screen memory, one **screen code** per cell (40 × 25) |
| `55296`–`56295` | Colour RAM, one colour 0–15 per cell |
| `53280` | Border colour |
| `53281` | Background colour |
| everything else | Ordinary 64 KB of RAM |

The cell in row *r*, column *c* sits at `1024 + 40*r + c`, its colour at
`55296 + 40*r + c`. Screen codes are **not** ASCII: 1 = `A`, 2 = `B` … 26 = `Z`,
32 = space, 48 = `0`. Codes **64–127 are the PETSCII graphics** — box drawing,
blocks, card suits — and adding 128 to any code shows it in reverse video.

Useful graphics codes: `64` ─ · `93` │ · `85` `73` `74` `75` the four corners ·
`91` ┼ · `81` ● · `87` ○ · `65` ♠ · `83` ♥ · `88` ♣ · `90` ♦ · `102` ▒ ·
`94` π. From `PRINT` they are reachable as `CHR$(160..255)`.

```basic
10 POKE 1024,85 : POKE 1024+1,64 : POKE 1024+2,73   : REM a box corner-line-corner
```

```basic
10 POKE 53281,0 : POKE 53280,11     : REM black screen, grey border
20 FOR I=0 TO 39
30   POKE 1024+40*8+I, I+1          : REM fill row 8 with A,B,C,...
40   POKE 55296+40*8+I, 1+(I AND 7) : REM each character a different colour
50 NEXT
```

Old listings from books and magazines therefore work the way they were meant to.

> One small deviation: `PEEK(53280)` returns a plain 0–15. The real VIC returns
> the unused top bits as ones (240 + colour), which would make
> `POKE 53280,PEEK(53280)+1` useless for cycling colours.

### Built-in functions

| Numbers | Meaning |
|---------|---------|
| `ABS(x)` `INT(x)` `SGN(x)` | Absolute value, round down, sign |
| `SQR(x)` `^` | Square root, power |
| `SIN` `COS` `TAN` `ATN` | Trigonometry (radians) |
| `EXP(x)` `LOG(x)` | Exponential, natural logarithm |
| `RND(x)` | Random number 0…1 |
| `PEEK(a)` | Read memory |

| Strings | Meaning |
|---------|---------|
| `LEN(a$)` | Length |
| `LEFT$(a$,n)` `RIGHT$(a$,n)` | Left / right *n* characters |
| `MID$(a$,s,n)` | *n* characters from position *s* (1-based) |
| `CHR$(n)` | Character for a code |
| `ASC(a$)` | Code of the first character |
| `STR$(n)` | Number → string |
| `VAL(a$)` | String → number |

Random number in the range 1…100: `INT(RND(1) * 100) + 1`.

---

## 5. Saving and loading programs

Programs are stored as plain text under `/GHOST/BASIC/NAME.PRG` — on the SD card,
otherwise in internal flash. So you can read them on a PC too.

```basic
SAVE "GAME"        saves the program in memory
LOAD "GAME"        loads it back (replaces the current program)
DIRECTORY          lists all programs (also: DIR or LOAD "$")
SCRATCH "GAME"     deletes a program
```

`DIRECTORY` looks like a 1541 disk directory:

```
0 "GHOST BASIC"     FL 2A
1    "GAME"                 PRG
100 BLOCKS FREE.
```

The volume id tells you where programs really go: **FL** = internal flash,
**SD** = memory card.

Filenames are upper-cased and truncated to 16 characters, just like the
original. A missing file gives `?FILE NOT FOUND ERROR`.

The rest of the layout, shared by every app: `/GHOST/DATA` for app data,
`/GHOST/SYS` for settings (including the paired keyboard), `/GHOST/APPS` with
one folder per app. The **Disk** app (under *Others*) shows space usage and the
number of programs — and can format the card.

---

## 6. Three complete examples

### Number guessing

```basic
10 N = INT(RND(1) * 100) + 1
20 PRINT "I AM THINKING OF 1 TO 100"
30 INPUT "YOUR GUESS"; T
40 IF T < N THEN PRINT "TOO SMALL" : GOTO 30
50 IF T > N THEN PRINT "TOO BIG" : GOTO 30
60 PRINT "CORRECT!"
```

### Quiz from DATA

```basic
10 READ F$, A$
20 IF F$ = "END" THEN PRINT "DONE" : END
30 PRINT F$
40 INPUT "ANSWER"; R$
50 IF R$ = A$ THEN PRINT "CORRECT" : GOTO 10
60 PRINT "WRONG - CORRECT: "; A$
70 GOTO 10
80 DATA "2+2", "4"
90 DATA "CAPITAL OF FRANCE", "PARIS"
100 DATA "END", "END"
```

### Multiplication trainer

```basic
10 FOR F = 1 TO 5
20   A = INT(RND(1) * 10) + 1
30   B = INT(RND(1) * 10) + 1
40   PRINT A; "*"; B; "=";
50   INPUT R
60   IF R = A * B THEN PRINT "CORRECT" : GOTO 80
70   PRINT "WRONG, IT IS"; A * B
80 NEXT F
90 PRINT "GREAT JOB!"
```

---

## 7. Commands and errors at a glance

**Statements:** `SAVE` `LOAD` `DIRECTORY` `SCRATCH` `PRINT` `?` `LET` `IF…THEN`
`FOR…TO…STEP` `NEXT` `GOTO` `GOSUB` `RETURN` `ON…GOTO/GOSUB` `INPUT` `GET`
`DIM` `DATA` `READ` `RESTORE` `POKE` `DEF FN` `REM` `END` `STOP` `CONT` `RUN`
`LIST` `NEW` `CLR`.

**Error messages** (original wording): `?SYNTAX ERROR` · `?DIVISION BY ZERO
ERROR` · `?UNDEF'D STATEMENT ERROR` (unknown line number) · `?NEXT WITHOUT FOR
ERROR` · `?RETURN WITHOUT GOSUB ERROR` · `?OUT OF DATA ERROR` · `?BAD SUBSCRIPT
ERROR` (array index too large) · `?TYPE MISMATCH ERROR` (number/string mixed up)
· `?FILE NOT FOUND ERROR` · `?DEVICE NOT PRESENT ERROR` (no storage found).

While a program runs, `IN <line>` is appended. `STOP` interrupts, `CONT` resumes.

---

## 8. What doesn't work yet

So you don't go looking for them — these original features are still missing:

- **Sound (SID).** The registers at 54272+ are plain RAM; nothing is audible.
- **Sprites and bitmap modes.** Screen memory, colour RAM and the two colour
  registers are mapped; the rest of the VIC-II is not.
- **The lowercase character set** and the Shift+Commodore switch between the
  two sets. The graphics half is there: all 128 screen codes are drawn, and
  bit 7 gives reverse video, so `POKE 1024,81+128` prints a reversed circle.
- **Control codes inside PRINT**, such as `CHR$(147)` to clear the screen or
  `CHR$(5)` to change colour mid-string.
- Integer variables `A%`, the jiffy clock `TI`/`TI$`, `SYS`, `WAIT`, `USR`, and
  file I/O beyond SAVE/LOAD (`OPEN`/`CLOSE`/`PRINT#`).

The core for writing small applications — maths, text, branching, loops, I/O,
arrays, data, subroutines, custom functions and poking the screen — is complete.
