// A BASIC correctness suite: runs many programs / direct commands against the
// real interpreter and checks their output against expected values.
#include "apps/ghost_basic/screen.h"
#include "apps/ghost_basic/basic.h"
#include "apps/ghost_basic/editor.h"
#include "apps/ghost_basic/petscii.h"
#include "board/display.h"

#include <cstdint>
#include <cstdio>
#include <deque>
#include <map>
#include <string>
#include <vector>

using namespace apps::ghost;

static Screen scr; static Basic basic(scr); static Editor ed(scr);
static std::deque<std::string> g_in;

// In-memory stand-in for the SD card, so SAVE/LOAD can be tested off-device.
struct FakeFiles final : public Files {
    std::map<std::string, std::string> f;
    bool save(const char* n, const std::string& t) override { f[n] = t; return true; }
    bool load(const char* n, std::string& o) override {
        auto it = f.find(n); if (it == f.end()) return false; o = it->second; return true; }
    bool list(std::vector<Entry>& o) override {
        for (auto& kv : f) o.push_back(Entry{kv.first, uint32_t(kv.second.size())});
        return true; }
    bool remove(const char* n) override { return f.erase(n) > 0; }
    uint32_t freeBytes() override { return 254 * 100; }
};
static FakeFiles g_files;

static int passN = 0, failN = 0;

static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(' ');
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(' ');
    return s.substr(a, b - a + 1);
}

static void feed(uint8_t k) {
    switch (basic.mode()) {
    case Basic::Mode::Running: basic.pushKey(k); break;
    case Basic::Mode::Input: break;
    case Basic::Mode::Idle: { char l[81]; if (ed.key(k, l, sizeof l)) basic.execLine(l); break; }
    }
}
static void pump() {
    int g = 0;
    while (basic.busy() && g++ < 300000) {
        if (basic.mode() == Basic::Mode::Input) {
            std::string r = g_in.empty() ? std::string() : g_in.front();
            if (!g_in.empty()) g_in.pop_front();
            for (char c : r) scr.put((uint8_t)c);
            scr.newLine();
            basic.provideInput(r.c_str());
        } else basic.poll();
    }
}
static void typeEnter(const std::string& s) {
    for (char c : s) feed((uint8_t)c);
    feed(0x0D);
    pump();
}

// Store `lines`, clear the screen, run `finalCmd`, return the output rows
// (between the command and READY.), each trimmed.
static std::vector<std::string> session(const std::vector<std::string>& lines,
                                        const std::string& finalCmd,
                                        const std::vector<std::string>& inputs = {}) {
    basic.reset(); scr.reset();
    for (const auto& l : lines) typeEnter(l);
    scr.reset();
    g_in.assign(inputs.begin(), inputs.end());
    typeEnter(finalCmd);
    std::vector<std::string> out;
    for (int r = 1; r < Screen::ROWS; ++r) {
        char b[81]; scr.readLine(r, b, sizeof b);
        std::string s = trim(b);
        if (s == "READY.") break;
        out.push_back(s);
    }
    while (!out.empty() && out.back().empty()) out.pop_back();
    return out;
}

// Run several commands in order; the screen is cleared before the last one so
// only its output is captured.
static std::vector<std::string> seq(const std::vector<std::string>& steps) {
    basic.reset(); scr.reset();
    for (std::size_t i = 0; i < steps.size(); ++i) {
        if (i + 1 == steps.size()) scr.reset();
        typeEnter(steps[i]);
    }
    std::vector<std::string> out;
    for (int r = 1; r < Screen::ROWS; ++r) {
        char b[81]; scr.readLine(r, b, sizeof b);
        std::string t = trim(b);
        if (t == "READY.") break;
        out.push_back(t);
    }
    while (!out.empty() && out.back().empty()) out.pop_back();
    return out;
}

static void checkSeq(const char* name, const std::vector<std::string>& steps,
                     const std::vector<std::string>& want) {
    auto got = seq(steps);
    bool ok = got.size() == want.size();
    for (std::size_t i = 0; ok && i < want.size(); ++i) ok = got[i] == want[i];
    if (ok) { ++passN; }
    else {
        ++failN;
        std::printf("FAIL  %s\n      got:     ", name);
        for (auto& x : got) std::printf("[%s] ", x.c_str());
        std::printf("\n      expected:");
        for (auto& x : want) std::printf("[%s] ", x.c_str());
        std::printf("\n");
    }
}

static void checkExpr(const std::string& expr, const std::string& want) {
    auto out = session({}, "PRINT " + expr);
    std::string got = out.empty() ? "<none>" : out[0];
    if (got == want) { ++passN; }
    else { ++failN; std::printf("FAIL  PRINT %s  -> [%s]  expected [%s]\n",
                                expr.c_str(), got.c_str(), want.c_str()); }
}

static void checkProg(const char* name, const std::vector<std::string>& lines,
                      const std::vector<std::string>& want,
                      const std::vector<std::string>& inputs = {}) {
    auto got = session(lines, "RUN", inputs);
    bool ok = got.size() == want.size();
    for (size_t i = 0; ok && i < want.size(); ++i) ok = got[i] == want[i];
    if (ok) { ++passN; }
    else {
        ++failN;
        std::printf("FAIL  %s\n      got:     ", name);
        for (auto& s : got) std::printf("[%s] ", s.c_str());
        std::printf("\n      expected:");
        for (auto& s : want) std::printf("[%s] ", s.c_str());
        std::printf("\n");
    }
}

int main() {
    basic.setFiles(&g_files);

    // --- arithmetic & precedence ---
    checkExpr("2+3*4", "14");
    checkExpr("(2+3)*4", "20");
    checkExpr("2^10", "1024");
    checkExpr("7/2", "3.5");
    checkExpr("10/2", "5");
    checkExpr("-3+2", "-1");
    checkExpr("2-3-4", "-5");
    checkExpr("2*3+4*5", "26");
    checkExpr("10-2*3", "4");
    checkExpr("100000", "100000");
    checkExpr("1/4", ".25");

    // --- numeric functions ---
    checkExpr("ABS(-7)", "7");
    checkExpr("INT(3.9)", "3");
    checkExpr("INT(-3.1)", "-4");
    checkExpr("SGN(-5)", "-1");
    checkExpr("SGN(0)", "0");
    checkExpr("SQR(144)", "12");

    // --- comparisons & logic ---
    checkExpr("5>3", "-1");
    checkExpr("5<3", "0");
    checkExpr("5=5", "-1");
    checkExpr("5<>5", "0");
    checkExpr("3<=3", "-1");
    checkExpr("(1=1)AND(2=3)", "0");
    checkExpr("(1=1)OR(2=3)", "-1");
    checkExpr("NOT 0", "-1");
    checkExpr("5AND3", "1");
    checkExpr("5OR2", "7");

    // --- strings ---
    checkExpr("LEN(\"HELLO\")", "5");
    checkExpr("LEFT$(\"COMMODORE\",4)", "COMM");
    checkExpr("RIGHT$(\"COMMODORE\",4)", "DORE");
    checkExpr("MID$(\"COMMODORE\",4,3)", "MOD");
    checkExpr("CHR$(65)", "A");
    checkExpr("ASC(\"A\")", "65");
    checkExpr("\"AB\"+\"CD\"", "ABCD");
    checkExpr("\"A\"<\"B\"", "-1");
    checkExpr("\"ABC\"=\"ABC\"", "-1");
    checkExpr("VAL(\"3.5\")+1", "4.5");
    checkExpr("STR$(42)", "42");
    checkExpr("LEN(\"\")", "0");

    // --- programs: loops ---
    checkProg("sum 1..10",
        {"10 S=0", "20 FOR I=1 TO 10:S=S+I:NEXT", "30 PRINT S"}, {"55"});
    checkProg("step -1",
        {"10 FOR I=3 TO 1 STEP -1:PRINT I:NEXT"}, {"3", "2", "1"});
    checkProg("nested for",
        {"10 FOR I=1 TO 2:FOR J=1 TO 2:PRINT I*10+J:NEXT:NEXT"},
        {"11", "12", "21", "22"});

    // --- control flow ---
    checkProg("if true/false",
        {"10 IF 5>3 THEN PRINT \"Y\"", "20 IF 5<3 THEN PRINT \"N\"", "30 PRINT \"E\""},
        {"Y", "E"});
    checkProg("gosub",
        {"10 GOSUB 100:PRINT \"B\":END", "100 PRINT \"A\":RETURN"}, {"A", "B"});
    checkProg("on goto",
        {"10 X=2:ON X GOTO 100,200", "100 PRINT \"ONE\":END", "200 PRINT \"TWO\":END"},
        {"TWO"});
    checkProg("goto loop with counter",
        {"10 I=0", "20 I=I+1:PRINT I", "30 IF I<3 THEN 20"}, {"1", "2", "3"});

    // --- arrays ---
    checkProg("array 1d",
        {"10 DIM A(3):A(0)=5:A(3)=9:PRINT A(0)+A(3)"}, {"14"});
    checkProg("array 2d",
        {"10 DIM M(2,2):M(1,1)=7:PRINT M(1,1)"}, {"7"});
    checkProg("array auto-dim",
        {"10 A(4)=8:PRINT A(4)"}, {"8"});

    // --- data / read ---
    checkProg("data read sum",
        {"10 FOR I=1 TO 3:READ X:S=S+X:NEXT:PRINT S", "20 DATA 10,20,30"}, {"60"});
    checkProg("read restore",
        {"10 READ A$:RESTORE:READ B$:PRINT A$+B$", "20 DATA HI"}, {"HIHI"});

    // --- def fn ---
    checkProg("def fn",
        {"10 DEF FN F(X)=X*X+1:PRINT FN F(4)"}, {"17"});

    // --- poke / peek ---
    checkProg("poke peek",
        {"10 POKE 5000,123:PRINT PEEK(5000)"}, {"123"});

    // --- variable 2-char significance ---
    checkProg("2-char vars",
        {"10 COUNT=1:COUNTER=2:PRINT COUNT"}, {"2"});

    // --- print formatting ---
    checkProg("print semicolons", {"10 PRINT 1;2;3"}, {"1  2  3"});
    checkProg("print tab", {"10 PRINT \"A\";TAB(5);\"B\""}, {"A    B"});
    checkProg("print spc", {"10 PRINT \"A\";SPC(3);\"B\""}, {"A   B"});
    checkProg("print comma", {"10 PRINT \"A\",\"B\""}, {std::string("A") + std::string(9, ' ') + "B"});

    // --- input ---
    checkProg("input numeric", {"10 INPUT A:PRINT A*2"}, {"? 21", "42"}, {"21"});
    checkProg("input string", {"10 INPUT A$:PRINT \"HI \";A$"}, {"? BOB", "HI BOB"}, {"BOB"});
    checkProg("input two", {"10 INPUT A,B:PRINT A+B"}, {"? 3,4", "7"}, {"3,4"});

    // --- get (empty when no key) ---
    checkProg("get empty", {"10 GET A$:IF A$=\"\" THEN PRINT \"EMPTY\""}, {"EMPTY"});

    // --- rnd range ---
    checkProg("rnd range 1..6",
        {"10 FOR I=1 TO 200:R=INT(RND(1)*6)+1:IF R<1 OR R>6 THEN PRINT \"BAD\":END",
         "20 NEXT:PRINT \"OK\""}, {"OK"});

    // --- errors ---
    checkProg("div by zero", {"10 PRINT 1/0"}, {"?DIVISION BY ZERO ERROR IN 10"});
    checkProg("undef statement", {"10 GOTO 999"}, {"?UNDEF'D STATEMENT ERROR IN 10"});
    checkProg("next without for", {"10 NEXT"}, {"?NEXT WITHOUT FOR ERROR IN 10"});
    checkProg("return without gosub", {"10 RETURN"}, {"?RETURN WITHOUT GOSUB ERROR IN 10"});
    checkProg("out of data", {"10 READ X:READ Y", "20 DATA 5"}, {"?OUT OF DATA ERROR IN 10"});
    checkProg("bad subscript", {"10 DIM A(2):A(5)=1"}, {"?BAD SUBSCRIPT ERROR IN 10"});

    // --- exponent precedence & associativity ---
    checkExpr("3^2", "9");
    checkExpr("-2^2", "-4");        // ^ binds tighter than unary minus
    checkExpr("2^-1", ".5");
    checkExpr("2*3^2", "18");
    checkExpr("2^3^2", "512");      // right-associative
    checkExpr("(-2)^2", "4");

    // --- more string edge cases ---
    checkExpr("MID$(\"HELLO\",3)", "LLO");
    checkExpr("MID$(\"HELLO\",2,2)", "EL");
    checkExpr("STR$(-5)", "-5");
    checkExpr("VAL(\"12ABC\")", "12");
    checkExpr("CHR$(65)+CHR$(66)", "AB");
    checkExpr("\"B\">\"A\"", "-1");
    checkExpr("LEN(STR$(100))", "4");

    // --- control-flow edge cases ---
    checkProg("for runs once at limit",
        {"10 FOR I=1 TO 0:PRINT I:NEXT:PRINT \"D\""}, {"1", "D"});
    checkProg("nested gosub",
        {"10 GOSUB 100:PRINT \"C\":END", "100 GOSUB 200:PRINT \"B\":RETURN",
         "200 PRINT \"A\":RETURN"}, {"A", "B", "C"});
    checkProg("mid-line gosub returns correctly",
        {"10 PRINT \"A\":GOSUB 100:PRINT \"C\":END", "100 PRINT \"B\":RETURN"},
        {"A", "B", "C"});
    checkProg("rem ignores rest",
        {"10 PRINT \"A\":REM PRINT X", "20 PRINT \"B\""}, {"A", "B"});
    checkProg("if multiple statements after then",
        {"10 IF 1 THEN A=5:PRINT A"}, {"5"});
    checkProg("if false skips rest of line",
        {"10 IF 0 THEN PRINT \"X\"", "20 PRINT \"AFTER\""}, {"AFTER"});
    checkProg("factorial via goto loop",
        {"10 N=5:F=1", "20 F=F*N:N=N-1:IF N>0 THEN 20", "30 PRINT F"}, {"120"});
    checkProg("nested loop count",
        {"10 T=0:FOR I=1 TO 3:FOR J=1 TO 3:T=T+1:NEXT:NEXT:PRINT T"}, {"9"});

    // --- string arrays & negative data ---
    checkProg("string array",
        {"10 DIM N$(3):N$(1)=\"HI\":N$(2)=\"YO\":PRINT N$(1)+N$(2)"}, {"HIYO"});
    checkProg("negative data",
        {"10 READ A,B:PRINT A+B", "20 DATA -5,3"}, {"-2"});

    // --- SAVE / LOAD / SCRATCH ---
    checkSeq("save+load round-trip",
        {"10 PRINT \"HI\"", "20 GOTO 10", "SAVE \"TEST\"", "NEW", "LOAD \"TEST\"", "LIST"},
        {"10 PRINT \"HI\"", "20 GOTO 10"});
    checkSeq("filename is upper-cased",
        {"10 PRINT 1", "SAVE \"abc\"", "NEW", "LOAD \"ABC\"", "LIST"}, {"10 PRINT 1"});
    checkSeq("load missing file",
        {"LOAD \"NOPE\""}, {"?FILE NOT FOUND ERROR"});
    checkSeq("scratch deletes",
        {"10 PRINT 1", "SAVE \"X\"", "SCRATCH \"X\"", "LOAD \"X\""},
        {"?FILE NOT FOUND ERROR"});
    checkSeq("load replaces the program",
        {"10 PRINT 1", "SAVE \"A\"", "NEW", "20 PRINT 2", "LOAD \"A\"", "LIST"},
        {"10 PRINT 1"});

    // --- DIRECTORY looks like a 1541 listing ---
    {
        g_files.f.clear();   // fresh "disk" for a predictable listing
        auto out = seq({"10 PRINT 1", "SAVE \"GAME\"", "DIRECTORY"});
        const bool ok = out.size() >= 3 &&
                        out.front().rfind("0 \"GHOST BASIC\"", 0) == 0 &&
                        out[1].find("\"GAME\"") != std::string::npos &&
                        out[1].find("PRG") != std::string::npos &&
                        out.back().find("BLOCKS FREE.") != std::string::npos;
        if (ok) ++passN;
        else { ++failN; std::printf("FAIL  directory  got: ");
               for (auto& x : out) std::printf("[%s] ", x.c_str()); std::printf("\n"); }
    }

    // --- memory map: POKE really reaches the screen ---
    // Screen and colour RAM must be poked and read back in one line: the
    // harness clears the screen between steps, which would wipe them.
    checkSeq("poke/peek screen ram", {"POKE 1024,5:PRINT PEEK(1024)"}, {"5"});
    checkSeq("poke/peek colour ram", {"POKE 55296,2:PRINT PEEK(55296)"}, {"2"});
    checkSeq("colour ram masks to 4 bits", {"POKE 55296,255:PRINT PEEK(55296)"}, {"15"});
    checkSeq("border register",      {"POKE 53280,0", "PRINT PEEK(53280)"}, {"0"});
    checkSeq("background register",  {"POKE 53281,6", "PRINT PEEK(53281)"}, {"6"});
    checkSeq("plain ram still works",{"POKE 4096,77", "PRINT PEEK(4096)"}, {"77"});

    // Screen codes 1 and 2 are "A" and "B": poking them must become visible
    // characters, which is the whole point of the exercise.
    {
        basic.reset(); scr.reset();
        typeEnter("POKE 1024+40*12,1:POKE 1024+40*12+1,2");
        char b[81]; scr.readLine(12, b, sizeof b);
        if (std::string(b) == "AB") ++passN;
        else { ++failN; std::printf("FAIL  poke writes characters  got [%s]\n", b); }
    }

    // One past the last cell is no longer screen — it is ordinary RAM, exactly
    // as on the real machine, and must not spill into the grid.
    checkSeq("past the screen is plain ram",
        {"POKE 2024,1", "PRINT PEEK(2024)"}, {"1"});

    // --- font table integrity -------------------------------------------
    // A single entry silently dropping out shifts every glyph after it. That
    // happened for real: a comment ending in a backslash swallowed the next
    // line, and codes 78..127 all slid down by one. Only two codes are blank
    // by design, so an all-zero glyph anywhere else means the table lost an
    // entry and the tail shifted.
    {
        int blanks = 0, firstBad = -1;
        for (int code = 0; code < 128; ++code) {
            bool empty = true;
            for (int r = 0; r < 8; ++r) if (petscii::FONT[code][r]) { empty = false; break; }
            if (!empty) continue;
            ++blanks;
            if (code != 32 && code != 96 && firstBad < 0) firstBad = code;
        }
        if (firstBad < 0 && blanks == 2) ++passN;
        else { ++failN; std::printf("FAIL  font table: %d blank glyphs, first unexpected at %d\n",
                                    blanks, firstBad); }
    }

    // Spot-check the codes real listings depend on.
    {
        struct { int code; uint8_t row0; } want[] = {
            { 64, 0x00}, { 81, 0x00}, { 91, 0x10}, { 93, 0x10}, {102, 0xAA}, {127, 0xF0},
        };
        bool ok = true;
        for (auto& w : want) if (petscii::FONT[w.code][0] != w.row0) ok = false;
        if (ok) ++passN;
        else { ++failN; std::printf("FAIL  font spot-check: a known glyph moved\n"); }
    }

    // --- list sorts and round-trips the source (entered out of order) ---
    {
        auto out = session({"20 GOTO 10", "10 PRINT \"HI\""}, "LIST");
        std::vector<std::string> want = {"10 PRINT \"HI\"", "20 GOTO 10"};
        bool ok = out.size() == want.size();
        for (size_t i = 0; ok && i < want.size(); ++i) ok = out[i] == want[i];
        if (ok) ++passN;
        else { ++failN; std::printf("FAIL  list  got: ");
               for (auto& s : out) std::printf("[%s] ", s.c_str()); std::printf("\n"); }
    }

    std::printf("\n===== %d passed, %d failed =====\n", passN, failN);
    return failN ? 1 : 0;
}
