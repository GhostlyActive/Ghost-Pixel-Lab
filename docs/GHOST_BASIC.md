Ghost BASIC – Manual

How to write small applications in BASIC on the Ghost-Pixel-Lab.
This manual describes exactly the language scope that is currently built in — nothing that isn't possible (yet) is promised here.

----------------------------------------------------------------------
1. Input

There is no on-screen keyboard. Typing is done in two ways.

Via the Serial Console (works immediately, no accessories needed)
> pio device monitor
Open Ghost BASIC and just start typing — the characters land directly on the device's screen. Ctrl+C acts as RUN/STOP.

Via a Bluetooth Keyboard
1. Open BLE Scan under Others.
2. Put the keyboard into pairing mode and hold it near the display — the strongest signal entry will be at the top.
3. Tap it. The device connects, verifies if it really is a keyboard, and remembers it. From then on, it connects automatically on every startup, without needing a scan.

Important: The ESP32-S3 has no Bluetooth Classic radio, only BLE. Keyboards that pair via Classic are invisible to this device and cannot work — the Keychron K2 Pro, for example, does not. The fact that a smartphone finds it proves nothing: its Bluetooth settings search for both. The only reliable test is: what shows up in the BLE scan list works. This is therefore also your pre-purchase test.

Special Keys
- RETURN: Enter / submit line
- Backspace: Delete character to the left
- Arrow keys: Move cursor
- Esc / Ctrl+C / BOOT Button: RUN/STOP – aborts a running program
- Home: Move cursor to top-left

The character set consists of uppercase letters (just like a powered-on C64). Whatever you type will appear in uppercase.

----------------------------------------------------------------------
2. The Two Modes

Direct Mode – you type a command without a line number, press RETURN, and it executes immediately:
PRINT 2+2
 4
READY.

Program Mode – you prefix a line number. The line is not executed immediately, but stored:
10 PRINT "HELLO"
20 GOTO 10
RUN

- RUN starts the program.
- LIST displays it (LIST 20 for a single line, LIST 10-30 for a range).
- NEW deletes the entire program.
- Entering a line number on its own (20 + RETURN) deletes that specific line.
- Entering the same number again replaces the line.

----------------------------------------------------------------------
3. The Editor – "The screen is the input"

There is no dedicated input field. You type directly onto the screen. RETURN parses the entire line the cursor is currently on anew. This means:
- You can move up to an old line with the arrow keys, change something, and press RETURN – the line is entered as new.
- A line can be up to 80 characters long (it visually wraps to two screen lines, but counts as one).

----------------------------------------------------------------------
4. Language Reference

Variables
- Numbers: A, X, NUMBER – Floating point.
- Strings: end with $ – A$, NAME$.
- Only the first two characters are significant: COUNT and COUNTER refer to the same variable.
10 A = 5
20 B$ = "TEXT"
30 PRINT A, B$

Expressions
- Math: +  -  *  /  ^ (Power), parentheses, signs.
- Comparisons: =  <>  <  >  <=  >= – return -1 (true) or 0 (false).
- Logic: AND  OR  NOT.
- Concatenate strings with +: "AB" + "CD" results in "ABCD".

PRINT
- ; appends directly, , jumps to the next column (grid of 10).
- Numbers get a leading space (placeholder for the sign).
- TAB(n) jumps to column n, SPC(n) prints n spaces.
- A ; at the end of a line suppresses the line break.
PRINT "X ="; X
PRINT "A", "B", "C"
PRINT TAB(10); "INDENTED"

Branches and Loops
10 IF A > 10 THEN PRINT "LARGE"
20 IF A = 0 THEN 100          : REM = GOTO 100
30 FOR I = 1 TO 10 STEP 2
40   PRINT I
50 NEXT I
60 ON X GOTO 100, 200, 300    : REM jumps to X (1,2,3)

- IF … THEN – if true, the rest of the line is executed; if false, it is skipped. THEN 100 or IF … GOTO 100 jump to a specific line.
- FOR … NEXT with optional STEP (can also be negative).
- GOSUB 500 … RETURN for subroutines.
- ON n GOTO/GOSUB list – selects the n-th jump destination.

Input
10 INPUT "WHAT IS YOUR NAME"; N$
20 PRINT "HELLO "; N$
30 INPUT "TWO NUMBERS"; A, B

- INPUT waits until you type a line and press RETURN. Multiple variables are read in, separated by commas.
- GET A$ reads one key without waiting (empty string if nothing is pressed). Typical wait loop:
10 GET A$ : IF A$ = "" THEN 10
20 PRINT "YOU PRESSED '"; A$; "'"

Arrays
10 DIM P(5)          : REM Numbers 0..5
20 DIM N$(10)        : REM Strings 0..10
30 DIM M(3, 3)       : REM Two-dimensional
Arrays used without DIM are automatically created with a size of 0..10 per dimension.

DATA / READ
10 FOR I = 1 TO 3 : READ W$(I) : NEXT
20 DATA "RED", "GREEN", "BLUE"
30 RESTORE            : REM Reset data pointer to start
READ fetches values from the DATA lines in sequence; RESTORE starts from the beginning again.

Custom Functions
10 DEF FN K(X) = X * X * X
20 PRINT FN K(3)      : REM 27

Memory: POKE / PEEK
10 POKE 4096, 65
20 PRINT PEEK(4096)   : REM 65
Note: POKE/PEEK currently address a dedicated 64-KB memory block — not the screen or actual hardware. So, POKE 1024,… does not (yet) change the image like it does on the original. Connection to screen RAM and device hardware will be added later.

Built-in Functions (Numbers)
- ABS(x), INT(x), SGN(x): Absolute value, round down, sign
- SQR(x), ^: Square root, power
- SIN, COS, TAN, ATN: Trigonometric functions (radians)
- EXP(x), LOG(x): Exponential function, natural logarithm
- RND(x): Random number 0…1
- PEEK(a): Read memory

Built-in Functions (Strings)
- LEN(a$): Length
- LEFT$(a$,n), RIGHT$(a$,n): Left / right n characters
- MID$(a$,s,n): n characters from position s (starting at 1)
- CHR$(n): Character for ASCII code
- ASC(a$): ASCII code of the first character
- STR$(n): Number -> String
- VAL(a$): String -> Number

Random number in the range 1…100: INT(RND(1) * 100) + 1.

----------------------------------------------------------------------
4b. Saving and Loading Programs

Programs are stored as text files under /GHOST/BASIC/NAME.PRG — on the SD card, otherwise in internal flash. This means you can also read them on a PC.
- SAVE "GAME" : saves the program in memory
- LOAD "GAME" : loads it back (replaces the current program)
- DIRECTORY   : shows all programs (also: DIR or LOAD "$")
- SCRATCH "GAME": deletes a program

DIRECTORY looks like a 1541 disk drive table of contents:
0 "GHOST BASIC"     GP 2A
1    "GAME"                 PRG
100 BLOCKS FREE.

Filenames are written in uppercase and truncated to 16 characters — just like the original. If the file is missing, you get ?FILE NOT FOUND ERROR.

The remaining folder structure (for other apps): /GHOST/DATA for app data, /GHOST/SYS for settings (including the paired keyboard), /GHOST/APPS with one folder per app. The Disk app (under Others) shows space usage and the number of programs — and can format the card.

----------------------------------------------------------------------
5. Three Complete Examples

Number Guessing
10 N = INT(RND(1) * 100) + 1
20 PRINT "I AM THINKING OF 1 TO 100"
30 INPUT "YOUR GUESS"; T
40 IF T < N THEN PRINT "TOO SMALL" : GOTO 30
50 IF T > N THEN PRINT "TOO BIG" : GOTO 30
60 PRINT "CORRECT!"

Quiz from DATA
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

Multiplication Table Trainer
10 FOR F = 1 TO 5
20   A = INT(RND(1) * 10) + 1
30   B = INT(RND(1) * 10) + 1
40   PRINT A; "*"; B; "=";
50   INPUT R
60   IF R = A * B THEN PRINT "CORRECT" : GOTO 80
70   PRINT "WRONG, IT IS"; A * B
80 NEXT F
90 PRINT "GREAT JOB!"

----------------------------------------------------------------------
6. Commands & Errors at a Glance

Statements: SAVE, LOAD, DIRECTORY, SCRATCH, PRINT, ?, LET, IF…THEN, FOR…TO…STEP, NEXT, GOTO, GOSUB, RETURN, ON…GOTO/GOSUB, INPUT, GET, DIM, DATA, READ, RESTORE, POKE, DEF FN, REM, END, STOP, CONT, RUN, LIST, NEW, CLR.

Error Messages (Original wording):
?SYNTAX ERROR, ?DIVISION BY ZERO ERROR, ?UNDEF'D STATEMENT ERROR (unknown line number), ?NEXT WITHOUT FOR ERROR, ?RETURN WITHOUT GOSUB ERROR, ?OUT OF DATA ERROR, ?BAD SUBSCRIPT ERROR (array index too large), ?TYPE MISMATCH ERROR (number/string mixed up), ?FILE NOT FOUND ERROR, ?DEVICE NOT PRESENT ERROR (no storage found).
While a program is running, IN <line> is appended. STOP interrupts, CONT resumes.

----------------------------------------------------------------------
7. What doesn't work (yet)

So you don't go looking for them – these original features are currently missing:
- Sound (SID) and Graphics/Sprites – POKE to screen/chip registers does not show anything yet (see note above).
- Integer variables A%, the Jiffy clock TI/TI$, SYS, WAIT, file OPEN/CLOSE.
- Lowercase/Graphics character set.

The core functionality for programming small applications – math, text, branching, loops, I/O, arrays, data, subroutines, custom functions – is fully intact.
