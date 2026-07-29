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

Whole listings can be pasted into the monitor in one go: the input path
buffers the burst (up to 8 KB) while the interpreter types it in, so nothing
is lost mid-line. Lines longer than 80 characters are cut at the C64's
logical-line limit, exactly as if typed by hand.

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
| `Shift+Home` | **CLR** — wipe the screen, cursor home (the C64's `SHIFT+CLR/HOME`) |

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
- **Integers:** end with `%` — `A%`. Values are truncated toward zero and must
  stay in −32768…32767, otherwise `?ILLEGAL QUANTITY ERROR`.
- Only the **first two characters** are significant: `COUNT` and `COUNTER` are
  the same variable. Flavour counts too, so `A`, `A$` and `A%` are three
  different variables.

Like the original, a `FOR` counter has to be floating point — `FOR I%=1 TO 10`
is a `?SYNTAX ERROR`.

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

### Control codes

A C64 program steers the screen by embedding control characters in the strings
it prints, and `CHR$()` produces them:

| Code | Effect |
|------|--------|
| `CHR$(147)` | clear the screen and go home |
| `CHR$(19)` | cursor home |
| `CHR$(17)` `CHR$(145)` | cursor down / up |
| `CHR$(29)` `CHR$(157)` | cursor right / left |
| `CHR$(18)` `CHR$(146)` | reverse video on / off |

The sixteen colours, in the order the original puts them on the number keys:

| Colour | Code | | Colour | Code |
|--------|------|-|--------|------|
| black | `144` | | orange | `129` |
| white | `5` | | brown | `149` |
| red | `28` | | light red | `150` |
| cyan | `159` | | dark grey | `151` |
| purple | `156` | | grey | `152` |
| green | `30` | | light green | `153` |
| blue | `31` | | light blue | `154` |
| yellow | `158` | | light grey | `155` |

```basic
10 PRINT CHR$(147)
20 PRINT CHR$(28);"RED ";CHR$(30);"GREEN ";CHR$(158);"YELLOW"
30 PRINT CHR$(18);"REVERSE";CHR$(146);" NORMAL"
```

A colour stays in effect until the next colour code, across `PRINT` statements.
Reverse mode is cancelled by `RETURN`, exactly like the original.

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
| `2040`–`2047` | Sprite shape pointers (see the sprite section) |
| `4096`–`6143` | The character generator "ROM", readable (see charsets) |
| `55296`–`56295` | Colour RAM, one colour 0–15 per cell |
| `53248`–`53294` | The VIC-II: sprites, modes, memory pointers |
| `53280` | Border colour |
| `53281` | Background colour |
| `54272`–`54300` | The SID |
| everything else | Ordinary 64 KB of RAM |

RAM is wiped by switching the machine on, **not** by RUN, NEW or CLR — a
sprite shape or charset you poke into memory stays there for the next program,
exactly as on the original.

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

### Sprites

The VIC-II's eight sprites live at 53248 upwards, at the original addresses. A
sprite is a 24 × 21 movable image with its own colour, placed by writing its
position, and switched on with one enable bit. The shape is 63 bytes in memory,
and a pointer says where — exactly as on the original.

| Address | Meaning |
|---------|---------|
| `53248 + 2*n` / `53249 + 2*n` | sprite *n* X and Y position |
| `53264` | 9th X bit per sprite (X ≥ 256) |
| `53269` | enable bits, one per sprite |
| `53271` / `53277` | expand Y / expand X (double size), one bit per sprite |
| `53276` | multicolour mode, one bit per sprite |
| `53278` | sprite/sprite collision — reading it clears it |
| `53285` / `53286` | the two shared multicolour colours |
| `53287…53294` | colour of sprites 0…7 |
| `2040…2047` | shape pointer per sprite: data sits at `pointer × 64` |

Positions carry the chip's real offset: a sprite at X = 24, Y = 50 has its
top-left pixel in the top-left corner of the text area. That is why listing
coordinates land where they always did.

```basic
10 V=53248                        : REM the VIC base
20 FOR I=0 TO 62:POKE 832+I,255:NEXT : REM a solid 24x21 block at 832
30 POKE 2040,13                   : REM sprite 0 shape -> 13*64 = 832
40 POKE V+39,1                    : REM sprite 0 colour: white
50 POKE V+21,1                    : REM enable sprite 0
60 X=24
70 POKE V,X : POKE V+1,100        : REM move it across
80 X=X+1 : IF X<255 THEN 70
```

Two enabled sprites whose pixels overlap set their bits in 53278; a game reads
it (`IF PEEK(53278) AND 1 THEN…`) and the read clears it for the next frame.
53279 is the same latch against the *background*: any sprite pixel on top of a
set glyph or bitmap pixel names its sprite there, and the read clears it. A
sprite whose bit is set in 53275 slips **behind** the text instead of covering
it — the classic way to walk a figure behind scenery. As on the chip, the %01
pair of a multicolour sprite is drawn but counts as background, so it neither
collides nor covers.

### A character set of your own

53272 ($D018) points the VIC at its character set. The built-in font sits at
4096 — and unlike the original, it is *readable* there, so copying it into RAM
needs none of the bank-switching dance from the old listings:

```basic
10 FOR I=0 TO 2047:POKE 12288+I,PEEK(4096+I):NEXT
20 POKE 53272,(PEEK(53272)AND240)OR12  : REM charset now at 12288
30 FOR I=0 TO 7:POKE 12288+8+I,255-PEEK(12288+8+I):NEXT : REM invert the 'A'
```

Each glyph is eight bytes, top to bottom, bit 7 leftmost; code × 8 indexes it.
Codes 128…255 are the second half of the set (the ROM keeps the reversed
glyphs there, which is all reverse video is). `POKE 53272,21` points back at
the built-in font; a poked set survives RUN and NEW, like any RAM.

### Hi-res graphics: the bitmap

Bit 5 of 53265 switches the VIC to the 320×200 bitmap; bit 3 of 53272 puts the
bitmap at 8192. Colours come per 8×8 cell from the text screen: the high
nibble is the ink, the low nibble the paper.

```basic
10 POKE 53265,PEEK(53265)OR32     : REM bitmap mode on
20 POKE 53272,PEEK(53272)OR8      : REM bitmap at 8192
30 FOR I=0 TO 999:POKE 1024+I,16*1+6:NEXT : REM white on blue everywhere
40 FOR I=8192 TO 16191:POKE I,0:NEXT      : REM clear the bitmap
50 FOR X=0 TO 319
60   Y=100+INT(40*SIN(X/20))
70   BY=8192+320*INT(Y/8)+8*INT(X/8)+(Y AND 7)
80   POKE BY,PEEK(BY) OR 2^(7-(X AND 7))
90 NEXT
100 GOTO 100
```

Line 70/80 is the classic plot formula, unchanged from the handbooks. With
bit 4 of 53270 set on top, the bitmap turns multicolour: 160×200 fat pixels,
each a two-bit pair — %01 and %10 from the colour cell's nibbles, %11 from
colour RAM, %00 the background register. Text mode has the same multicolour
trick: colour-RAM values 8…15 switch a cell to pairs drawn from 53282/53283
and the cell colour. `POKE 53265,PEEK(53265)AND239` blanks the whole display
to the border colour (bit 4 is the display-enable); the raster register 53266
returns a moving value so `WAIT 53266,…` terminates, but there is no real beam
behind it.

### Sound: the SID

Three voices live at 54272 upwards, at the original addresses. `S=54272` and
then offsets is how every listing does it:

| Offset | Register |
|--------|----------|
| `S+0` `S+1` | voice 1 frequency, low and high byte |
| `S+2` `S+3` | pulse width, low and high |
| `S+4` | waveform + gate (see below) |
| `S+5` | attack (high nibble) / decay (low nibble) |
| `S+6` | sustain (high) / release (low) |
| `S+7`… | voice 2, same seven registers; `S+14`… voice 3 |
| `S+24` | master volume, 0–15 |
| `S+27` | voice 3 oscillator output — the classic random-number source |

Waveform bits for `S+4`: `16` triangle, `32` sawtooth, `64` pulse, `128` noise.
Add `1` to open the gate and start the note, clear it to release.

```basic
10 S=54272
20 POKE S+24,15            : REM volume up
30 POKE S+5,9              : REM quick attack, short decay
40 POKE S+6,0              : REM no sustain — a plucked sound
50 POKE S+1,25 : POKE S,177: REM about 386 Hz
60 POKE S+4,33             : REM sawtooth + gate on
70 FOR T=1 TO 300 : NEXT
80 POKE S+4,32             : REM gate off, let it release
```

Ring modulation (`+4` on the control register) and oscillator sync (`+2`) work
and read the neighbouring voice, so the metallic and hard-sync timbres come out.
The envelope follows the chip's shape: attack is linear, decay and release run
an exponential ladder (fast at the top, long dying tail), and outside attack
the level only ever falls — lowering sustain mid-note pulls the voice down,
raising it does nothing until the next gate.
`PEEK(S+27)` returns the voice 3 oscillator, which listings use as a random
source — set voice 3 to noise and read it.

The filter works the way the listings drive it:

| Offset | Register |
|--------|----------|
| `S+21` | cutoff, low 3 bits |
| `S+22` | cutoff, high 8 bits |
| `S+23` | resonance (high nibble) + routing: bits 0–2 send voices 1–3 in |
| `S+24` | low nibble volume; `+16` low-pass, `+32` band-pass, `+64` high-pass, `+128` mutes an unfiltered voice 3 |

```basic
10 S=54272
20 POKE S+24,15+16         : REM volume, low-pass on
30 POKE S+23,1             : REM voice 1 through the filter
40 POKE S+5,9 : POKE S+6,240
50 POKE S+1,25 : POKE S,177 : POKE S+4,33
60 FOR C=0 TO 255 : POKE S+22,C : NEXT : REM sweep the cutoff open
70 POKE S+4,32
```

A voice routed in with no mode bit set disappears from the mix, and `+128`
silences voice 3 while `PEEK(S+27)` keeps ticking — the silent-LFO trick, both
exactly as on the chip.

> One honest caveat: the real 6581's cutoff curve varied so wildly between
> chips that no two C64s sounded the same. The mapping here (about 30 Hz to
> 6 kHz, resonance as mild as the original's) is *one* plausible chip, not
> every chip.

### Built-in functions

| Numbers | Meaning |
|---------|---------|
| `ABS(x)` `INT(x)` `SGN(x)` | Absolute value, round down, sign |
| `SQR(x)` `^` | Square root, power |
| `SIN` `COS` `TAN` `ATN` | Trigonometry (radians) |
| `EXP(x)` `LOG(x)` | Exponential, natural logarithm |
| `RND(x)` | Random number 0…1 |
| `PEEK(a)` | Read memory |
| `FRE(x)` | Free bytes (see below) |
| `POS(x)` | Current cursor column |

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

### Reserved variables

| Name | Meaning |
|------|---------|
| `TI` | Jiffy clock — 1/60 s ticks, wraps after 24 h. Read-only. |
| `TI$` | The same clock as `"HHMMSS"`. Assign to it to set the time. |
| `ST` | Status of the last I/O operation. Always 0 for now. |

```basic
10 TI$="000000"
20 FOR I=1 TO 1000 : NEXT
30 PRINT "THAT TOOK";TI;"JIFFIES"
```

`FRE(0)` reports the free part of the advertised 38911 bytes — and reproduces
the original's most notorious quirk: the value comes back through a signed
16-bit register, so an almost empty machine reports a **negative** number. Add
65536 to get the real figure.

`WAIT addr, mask [,xor]` pauses until `(PEEK(addr) XOR xor) AND mask` is
non-zero. RUN/STOP still breaks out of it.

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

### Data files

Beyond whole programs, a file can be opened as a stream of lines:

```basic
10 OPEN 1,8,2,"SCORES,S,W"     : REM S,W = sequential, write
20 PRINT#1,"MARLON"
30 PRINT#1,9500
40 CLOSE 1
50 OPEN 1,8,2,"SCORES,S,R"     : REM S,R = read
60 INPUT#1,N$
70 INPUT#1,P
80 CLOSE 1
90 PRINT N$;" SCORED";P
```

| Statement | Meaning |
|-----------|---------|
| `OPEN lf,dev,sa,"NAME,S,W"` | open logical file *lf* for writing |
| `OPEN lf,dev,sa,"NAME,S,R"` | open it for reading |
| `PRINT# lf, …` | like `PRINT`, but into the file |
| `INPUT# lf, vars` | read one line, split on commas |
| `GET# lf, a$` | read a single character |
| `CMD lf` | send *all* `PRINT` output to the file until `CLOSE` |
| `CLOSE lf` | close it — a write file is only saved here |
| `VERIFY "NAME"` | compare the program in memory with the file |

`ST` becomes **64** once a read passes the end of the file, which is the usual
way to stop:

```basic
10 OPEN 1,8,2,"SCORES,S,R"
20 INPUT#1,A$ : PRINT A$
30 IF ST=0 THEN 20
40 CLOSE 1
```

The device and secondary-address numbers are accepted and ignored — there is
one drive here — so `LOAD "NAME",8` and `SAVE "NAME",8,1` work as written. A
write file is held in memory and flushed on `CLOSE`, so a program that forgets
to close loses that file.

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

**Statements:** `SAVE` `LOAD` `VERIFY` `DIRECTORY` `SCRATCH` `PRINT` `?` `LET`
`IF…THEN` `FOR…TO…STEP` `NEXT` `GOTO` `GOSUB` `RETURN` `ON…GOTO/GOSUB` `INPUT`
`GET` `DIM` `DATA` `READ` `RESTORE` `POKE` `WAIT` `DEF FN` `REM` `END` `STOP`
`CONT` `RUN` `LIST` `NEW` `CLR`.

**File I/O:** `OPEN` `CLOSE` `PRINT#` `INPUT#` `GET#` `CMD` — see *Data files*
in section 5.

**Error messages** (original wording): `?SYNTAX ERROR` · `?DIVISION BY ZERO
ERROR` · `?UNDEF'D STATEMENT ERROR` (unknown line number) · `?NEXT WITHOUT FOR
ERROR` · `?RETURN WITHOUT GOSUB ERROR` · `?OUT OF DATA ERROR` · `?BAD SUBSCRIPT
ERROR` (array index too large) · `?TYPE MISMATCH ERROR` (number/string mixed up)
· `?FILE NOT FOUND ERROR` · `?DEVICE NOT PRESENT ERROR` (no storage found) ·
`?ILLEGAL QUANTITY ERROR` (a value outside what the function accepts, e.g.
`SQR(-1)`, `LOG(0)`, `ASC("")`, `MID$(a$,0)`) · `?OVERFLOW ERROR` ·
`?REDIM'D ARRAY ERROR` (dimensioning the same array twice) · `?FILE OPEN ERROR`
· `?FILE NOT OPEN ERROR` · `?UNDEF'D FUNCTION ERROR`.

While a program runs, `IN <line>` is appended. `STOP` interrupts, `CONT` resumes.

---

## 8. What doesn't work yet

So you don't go looking for them — these original features are still missing:

- **Raster tricks and interrupts.** The raster register moves so WAIT loops
  terminate, but there is no beam and no IRQ — split-screen and border-opening
  effects need machine code anyway, and there is none here by choice.
- **Smooth scrolling and ECM.** The low bits of 53265/53270 (pixel scroll,
  38-column/24-row mode, extended colour mode) are stored but not drawn.
- **VIC banking.** The VIC always sees bank 0 (0–16383); 56576 ($DD00) is
  plain RAM. Screen, charsets, bitmap and sprite shapes all fit there, which
  is where the listings put them anyway.
- **The lowercase character set** and the Shift+Commodore switch between the
  two sets. The graphics half is there: all 128 screen codes are drawn, and
  bit 7 gives reverse video, so `POKE 1024,81+128` prints a reversed circle.
  (A custom charset at 6144 would be the way to build one yourself.)
- `SYS` and `USR` — they call machine code, and there is no 6502 here by choice.

The language is complete, and so is the machine a listing reaches through
POKE: screen, colour RAM, sprites with priority and both collisions, custom
character sets, the two bitmap modes, and the SID with its filter.
