// A BASIC correctness suite: runs many programs / direct commands against the
// real interpreter and checks their output against expected values.
#include "apps/ghost_basic/screen.h"
#include "apps/ghost_basic/basic.h"
#include "apps/ghost_basic/editor.h"
#include "apps/ghost_basic/petscii.h"
#include "apps/ghost_basic/sid.h"
#include "board/display.h"
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
static Sid g_sid;
static Vic g_spr;

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
    std::string got = out.empty() ? "" : out[0];   // an empty line is a real result
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
    basic.setSid(&g_sid);
    basic.setVic(&g_spr);

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

    // --- numeric functions, edge cases included ---------------------------
    checkExpr("ABS(0)", "0");            checkExpr("ABS(-0.5)", ".5");
    checkExpr("INT(0)", "0");            checkExpr("INT(-0.1)", "-1");
    checkExpr("INT(5)", "5");            checkExpr("INT(-5)", "-5");
    checkExpr("SGN(7)", "1");            checkExpr("SGN(-0.001)", "-1");
    checkExpr("SQR(0)", "0");            checkExpr("SQR(2)", "1.41421356");
    checkExpr("SQR(-1)", "?ILLEGAL QUANTITY ERROR");
    checkExpr("LOG(1)", "0");            checkExpr("LOG(0)", "?ILLEGAL QUANTITY ERROR");
    checkExpr("LOG(-1)", "?ILLEGAL QUANTITY ERROR");
    checkExpr("EXP(0)", "1");
    checkExpr("SIN(0)", "0");            checkExpr("COS(0)", "1");
    checkExpr("TAN(0)", "0");            checkExpr("ATN(0)", "0");
    checkExpr("INT(SIN(1)*1000)", "841");

    // --- string functions at their boundaries -----------------------------
    checkExpr("LEFT$(\"AB\",0)", "");
    checkExpr("LEFT$(\"AB\",9)", "AB");
    checkExpr("RIGHT$(\"AB\",0)", "");
    checkExpr("RIGHT$(\"AB\",9)", "AB");
    checkExpr("MID$(\"ABCDE\",5)", "E");
    checkExpr("MID$(\"ABCDE\",9)", "");
    checkExpr("MID$(\"ABCDE\",2,0)", "");
    checkExpr("MID$(\"ABCDE\",0,2)", "?ILLEGAL QUANTITY ERROR");
    checkExpr("LEN(CHR$(0))", "1");
    checkExpr("ASC(\"\")", "?ILLEGAL QUANTITY ERROR");
    checkExpr("VAL(\"\")", "0");
    checkExpr("VAL(\"-3.5\")", "-3.5");
    checkExpr("VAL(\"  7\")", "7");
    checkExpr("CHR$(65)+CHR$(65)", "AA");
    checkExpr("LEN(\"A\"+\"B\"+\"C\")", "3");
    checkExpr("STR$(0)", "0");
    checkExpr("STR$(.5)", ".5");
    checkExpr("LEN(STR$(-1))", "2");
    checkExpr("VAL(STR$(1234))", "1234");

    // --- how numbers come out ---------------------------------------------
    checkExpr("1/3", ".333333333");
    checkExpr("2/3", ".666666667");
    checkExpr("1/2", ".5");
    checkExpr("-1/2", "-.5");
    checkExpr("1000000", "1000000");
    checkExpr("0.1+0.2", ".3");
    checkExpr("3*0.1", ".3");
    checkExpr("1E3", "1000");
    checkExpr("-0", "0");

    // --- comparisons and logic, more corners -------------------------------
    checkExpr("\"\"=\"\"", "-1");
    checkExpr("\"A\"=\"AB\"", "0");
    checkExpr("\"AB\">\"A\"", "-1");
    checkExpr("NOT -1", "0");
    checkExpr("NOT 1", "-2");
    checkExpr("-1 AND -1", "-1");
    checkExpr("0 OR 0", "0");
    checkExpr("(5>3)AND(2>1)", "-1");
    checkExpr("(5>3)+(2>1)", "-2");
    checkExpr("ABS((5>3)+(2>1))", "2");

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

    // --- loops, nesting and their corners ---------------------------------
    checkProg("three nested loops",
        {"10 T=0", "20 FOR I=1 TO 2:FOR J=1 TO 2:FOR K=1 TO 2:T=T+1:NEXT:NEXT:NEXT",
         "30 PRINT T"}, {"8"});
    checkProg("NEXT may name its variable",
        {"10 FOR I=1 TO 3:PRINT I:NEXT I"}, {"1", "2", "3"});
    // NEXT J,I closes two loops in one statement, exactly like NEXT J : NEXT I.
    checkProg("NEXT closes several loops at once",
        {"10 FOR I=1 TO 2", "20 FOR J=1 TO 2", "30 PRINT I*10+J", "40 NEXT J,I",
         "50 PRINT \"D\""}, {"11", "12", "21", "22", "D"});
    // A comma list runs left to right: the inner variable has to come first.
    checkProg("NEXT J,I visits the inner loop first",
        {"10 FOR I=1 TO 2:FOR J=1 TO 2:T=T+1:NEXT J,I", "20 PRINT T"}, {"4"});
    // Naming an outer variable abandons the inner loop still on the stack — the
    // C64 way of breaking out of a nested loop early.
    checkProg("NEXT of an outer var drops the inner loop",
        {"10 FOR I=1 TO 3", "20 FOR J=1 TO 9", "30 PRINT I", "40 NEXT I",
         "50 PRINT \"D\""}, {"1", "2", "3", "D"});
    checkProg("NEXT with an unknown variable is an error",
        {"10 FOR I=1 TO 3:NEXT K"}, {"?NEXT WITHOUT FOR ERROR IN 10"});
    checkProg("the counter survives the loop",
        {"10 FOR I=1 TO 3:NEXT", "20 PRINT I"}, {"4"});
    checkProg("a negative STEP that never runs still runs once",
        {"10 FOR I=1 TO 5 STEP -1:PRINT I:NEXT:PRINT \"D\""}, {"1", "D"});
    checkProg("fractional STEP",
        {"10 FOR I=1 TO 2 STEP .5:PRINT I:NEXT"}, {"1", "1.5", "2"});
    checkProg("three levels of GOSUB",
        {"10 GOSUB 100:PRINT \"D\":END", "100 GOSUB 200:RETURN",
         "200 GOSUB 300:RETURN", "300 PRINT \"A\":RETURN"}, {"A", "D"});

    // --- ON, DIM and DATA corners -----------------------------------------
    checkProg("ON 0 falls through",
        {"10 ON 0 GOTO 100", "20 PRINT \"FELL\":END", "100 PRINT \"JUMPED\""}, {"FELL"});
    checkProg("ON past the end of the list falls through",
        {"10 ON 5 GOTO 100,110", "20 PRINT \"FELL\":END", "100 PRINT \"A\"", "110 PRINT \"B\""},
        {"FELL"});
    checkProg("ON with a negative index is an error",
        {"10 ON -1 GOTO 100", "20 END", "100 PRINT \"X\""},
        {"?ILLEGAL QUANTITY ERROR IN 10"});
    checkProg("ON GOSUB comes back",
        {"10 ON 2 GOSUB 100,200", "20 PRINT \"BACK\":END",
         "100 PRINT \"ONE\":RETURN", "200 PRINT \"TWO\":RETURN"}, {"TWO", "BACK"});
    checkProg("three-dimensional arrays",
        {"10 DIM A(2,2,2):A(1,1,1)=9:PRINT A(1,1,1)"}, {"9"});
    checkProg("dimensioning twice is an error",
        {"10 DIM A(5)", "20 DIM A(5)"}, {"?REDIM'D ARRAY ERROR IN 20"});
    checkProg("RESTORE in the middle of reading",
        {"10 READ A:READ B:RESTORE:READ C", "20 PRINT A;B;C", "30 DATA 1,2,3"},
        {"1  2  1"});
    checkProg("READ mixes strings and numbers",
        {"10 READ A$,B:PRINT A$;B", "20 DATA HI,7"}, {"HI 7"});

    // --- PRINT layout ------------------------------------------------------
    checkProg("a bare PRINT makes a blank line",
        {"10 PRINT \"A\":PRINT:PRINT \"B\""}, {"A", "", "B"});
    checkProg("a trailing semicolon holds the line",
        {"10 PRINT \"A\";:PRINT \"B\""}, {"AB"});
    checkProg("two commas cross two zones",
        {"10 PRINT \"A\",\"B\",\"C\""},
        {std::string("A") + std::string(9, ' ') + "B" + std::string(9, ' ') + "C"});

    // --- editing a stored program -----------------------------------------
    checkSeq("a line number on its own deletes the line",
        {"10 PRINT 1", "20 PRINT 2", "10", "LIST"}, {"20 PRINT 2"});
    checkSeq("entering the same number replaces it",
        {"10 PRINT 1", "10 PRINT 9", "LIST"}, {"10 PRINT 9"});
    checkSeq("LIST takes a range",
        {"10 PRINT 1", "20 PRINT 2", "30 PRINT 3", "LIST 20-30"},
        {"20 PRINT 2", "30 PRINT 3"});
    checkSeq("LIST takes a single line",
        {"10 PRINT 1", "20 PRINT 2", "LIST 10"}, {"10 PRINT 1"});

    // --- every error message the machine can print -------------------------
    checkSeq("error: syntax",         {"PRINT )"},            {"?SYNTAX ERROR"});
    checkSeq("error: divide by zero", {"PRINT 1/0"},          {"?DIVISION BY ZERO ERROR"});
    checkSeq("error: type mismatch",  {"A=\"X\""},           {"?TYPE MISMATCH ERROR"});
    checkSeq("error: undefined function", {"PRINT FN Q(1)"},  {"?UNDEF'D FUNCTION ERROR"});
    checkSeq("error: can't continue", {"CONT"},               {"?CAN'T CONTINUE ERROR"});
    checkSeq("error: file not open",  {"CLOSE 1:PRINT#1,\"X\""}, {"?FILE NOT OPEN ERROR"});
    checkSeq("error: overflow",       {"PRINT EXP(200)"},     {"?OVERFLOW ERROR"});

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

    // --- integer variables (A%) ------------------------------------------
    checkExpr("0", "0");                       // sanity anchor
    checkSeq("integer truncates toward zero", {"A%=3.7:PRINT A%"}, {"3"});
    checkSeq("integer truncates negatives up", {"A%=-3.7:PRINT A%"}, {"-3"});
    checkSeq("A and A% are different variables", {"A=5:A%=7:PRINT A;A%"}, {"5  7"});
    checkSeq("integer range is enforced", {"A%=40000"}, {"?ILLEGAL QUANTITY ERROR"});
    checkSeq("integer arrays truncate too",
        {"DIM A%(3):A%(1)=2.9:PRINT A%(1)"}, {"2"});
    checkProg("integer loop counter is rejected, like the original",
        {"10 FOR I%=1 TO 3", "20 NEXT"}, {"?SYNTAX ERROR IN 10"});
    checkProg("INPUT into an integer truncates",
        {"10 INPUT A%", "20 PRINT A%"}, {"? 7.9", "7"}, {"7.9"});

    // --- line lengths at the wrap boundary --------------------------------
    // 40 characters fill a row exactly, 41 spill into a linked second row, and
    // 80 is the documented ceiling. Each of these has broken before.
    {
        const std::string p33(33, 'A'), p34(34, 'A'), p73(73, 'A');
        checkSeq("a line of exactly 40 characters", {"10 REM " + p33, "LIST"},
                 {"10 REM " + p33});
        // 41 characters cannot fit one 40-column row, so LIST necessarily
        // shows them across two — the stored line is still one line.
        checkSeq("a line that wraps to a second row", {"10 REM " + p34, "LIST"},
                 {"10 REM " + std::string(33, 'A'), "A"});
        checkSeq("a line of the full 80 characters", {"10 REM " + p73, "LIST"},
                 {"10 REM " + std::string(33, 'A'), std::string(40, 'A')});
    }

    // --- cursor movement at the edges --------------------------------------
    {
        basic.reset(); scr.reset();
        feed('A'); feed('B');                 // row 0, cursor at column 2
        feed(KEY_CRSR_LEFT); feed(KEY_CRSR_LEFT); feed(KEY_CRSR_LEFT);
        const bool wrapped = scr.cursorY() == 0 && scr.cursorX() == 0;  // home stays home
        scr.setCursor(0, 1);
        feed(KEY_CRSR_LEFT);                  // off the left edge of row 1
        const bool toPrevRow = scr.cursorY() == 0 && scr.cursorX() == Screen::COLS - 1;
        if (wrapped && toPrevRow) ++passN;
        else { ++failN; std::printf("FAIL  cursor does not wrap at the left edge\n"); }
    }
    {
        basic.reset(); scr.reset();
        scr.setCursor(Screen::COLS - 1, 0);
        feed(KEY_CRSR_RIGHT);                 // off the right edge
        if (scr.cursorY() == 1 && scr.cursorX() == 0) ++passN;
        else { ++failN; std::printf("FAIL  cursor does not wrap at the right edge\n"); }
    }
    {
        basic.reset(); scr.reset();
        feed('A'); feed('B'); feed(KEY_DELETE);
        char b[81]; scr.readLine(0, b, sizeof b);
        if (std::string(b) == "A" && scr.cursorX() == 1) ++passN;
        else { ++failN; std::printf("FAIL  DELETE gave [%s] at column %d\n", b, scr.cursorX()); }
    }

    // --- the editor's signature move --------------------------------------
    // Cursor up onto an old line, change a character, press RETURN and the
    // line is re-entered. This is what makes the machine feel like a C64, and
    // until now nothing tested it.
    {
        basic.reset(); scr.reset();
        typeEnter("10 PRINT 1");            // lands on row 0, cursor drops to row 1
        feed(KEY_CRSR_UP);                  // back onto the line
        for (int i = 0; i < 9; ++i) feed(KEY_CRSR_RIGHT);   // out to the digit
        feed('2');                          // overwrite it
        feed(0x0D);                         // RETURN re-enters the whole line
        scr.reset();
        typeEnter("LIST");
        char b[81]; scr.readLine(1, b, sizeof b);
        if (std::string(b) == "10 PRINT 2") ++passN;
        else { ++failN; std::printf("FAIL  editor: re-entering an edited line gave [%s]\n", b); }
    }

    // --- the screen scrolls once it is full --------------------------------
    {
        basic.reset(); scr.reset();
        typeEnter("10 FOR I=1 TO 30:PRINT I:NEXT");
        scr.reset();
        typeEnter("RUN");
        char top[81]; scr.readLine(0, top, sizeof top);
        bool sawLast = false;
        for (int r = 0; r < Screen::ROWS; ++r) {
            char b[81]; scr.readLine(r, b, sizeof b);
            if (trim(b) == "30") sawLast = true;
        }
        if (std::string(top) != "RUN" && sawLast) ++passN;
        else { ++failN; std::printf("FAIL  screen: 30 lines did not scroll properly\n"); }
    }

    // --- an endless loop must stay interruptible ---------------------------
    // The whole point of the stepped interpreter: 10 GOTO 10 may never freeze
    // the frame loop, and RUN/STOP has to get back out of it.
    {
        basic.reset(); scr.reset();
        typeEnter("10 GOTO 10");
        for (char c : std::string("RUN")) feed((uint8_t)c);
        feed(0x0D);
        for (int i = 0; i < 5; ++i) basic.poll();
        const bool stillRunning = basic.busy();
        basic.breakRun();
        basic.poll();
        if (stillRunning && !basic.busy()) ++passN;
        else { ++failN; std::printf("FAIL  endless loop: running=%d, stopped=%d\n",
                                    stillRunning, !basic.busy()); }
    }

    // --- malformed input must not wedge the machine ------------------------
    {
        static const char* junk[] = {
            "PRINT )))", "FOR", "NEXT", "GOTO", "IF", "A=", "DIM A(", "POKE ,",
            "PRINT CHR$(", "LEFT$(\"A\"", "GOSUB", "RETURN", "ON", "OPEN",
            "INPUT#", "DEF FN", "WAIT", "?????", "10", "LIST -", "\"",
        };
        bool ok = true;
        for (const char* j : junk) {
            basic.reset(); scr.reset();
            typeEnter(j);
            if (basic.busy()) { ok = false; std::printf("  wedged on: %s\n", j); }
        }
        if (ok) ++passN;
        else { ++failN; std::printf("FAIL  garbage input left the interpreter busy\n"); }
    }

    // --- every glyph renders exactly what the table holds -------------------
    // Guards the whole font->screen path, which is how the shifted-codes bug
    // stayed invisible for three rounds of looking at pictures.
    {
        static std::vector<uint16_t> fb(board::display::WIDTH * board::display::HEIGHT);
        board::gfx::Surface surf{fb.data(), board::display::WIDTH, board::display::HEIGHT};
        const int ox = (board::display::WIDTH  - Screen::PIXW) / 2;
        const int oy = (board::display::HEIGHT - Screen::PIXH) / 2;
        scr.reset(); scr.setBackground(COL_BLACK); scr.setTextColor(COL_WHITE);
        int bad = -1;
        for (int code = 0; code < 128 && bad < 0; ++code) {
            scr.pokeScreen(0, uint8_t(code));
            scr.pokeColor(0, COL_WHITE);
            scr.render(surf, false);
            for (int r = 0; r < 8; ++r) {
                uint8_t got = 0;
                for (int c = 0; c < 8; ++c)
                    if (fb[(oy + r) * board::display::WIDTH + ox + c] != 0) got |= uint8_t(0x80 >> c);
                if (got != petscii::FONT[code][r]) { bad = code; break; }
            }
        }
        if (bad < 0) ++passN;
        else { ++failN; std::printf("FAIL  glyph %d does not render as the table says\n", bad); }
    }

    // --- the programs the manual promises ----------------------------------
    // If the documentation prints a listing, the suite runs it. Otherwise the
    // manual slowly drifts into fiction.
    checkProg("manual: quiz from DATA",
        {"10 READ F$, A$", "20 IF F$ = \"END\" THEN PRINT \"DONE\" : END", "30 PRINT F$",
         "40 INPUT \"ANSWER\"; R$", "50 IF R$ = A$ THEN PRINT \"CORRECT\" : GOTO 10",
         "60 PRINT \"WRONG - CORRECT: \"; A$", "70 GOTO 10",
         "80 DATA \"2+2\", \"4\"", "90 DATA \"CAPITAL OF FRANCE\", \"PARIS\"",
         "100 DATA \"END\", \"END\""},
        {"2+2", "ANSWER? 4", "CORRECT", "CAPITAL OF FRANCE", "ANSWER? LONDON",
         "WRONG - CORRECT: PARIS", "DONE"},
        {"4", "LONDON"});

    checkProg("manual: number guessing (with the draw fixed)",
        {"10 N = 42", "20 PRINT \"I AM THINKING OF 1 TO 100\"", "30 INPUT \"YOUR GUESS\"; T",
         "40 IF T < N THEN PRINT \"TOO SMALL\" : GOTO 30",
         "50 IF T > N THEN PRINT \"TOO BIG\" : GOTO 30", "60 PRINT \"CORRECT!\""},
        {"I AM THINKING OF 1 TO 100", "YOUR GUESS? 50", "TOO BIG",
         "YOUR GUESS? 25", "TOO SMALL", "YOUR GUESS? 42", "CORRECT!"},
        {"50", "25", "42"});

    // --- SID ---------------------------------------------------------------
    {
        // The classic listing: volume, envelope, pitch, then gate on.
        auto sidProgram = [](const char* extra) {
            basic.reset(); scr.reset(); g_sid.reset(); g_sid.setSampleRate(16000);
            typeEnter("10 S=54272");
            typeEnter("20 POKE S+24,15");
            typeEnter("30 POKE S+5,0 : POKE S+6,240");   // instant attack, full sustain
            typeEnter("40 POKE S+1,25 : POKE S,177");
            typeEnter(extra);
            typeEnter("RUN");
        };

        // A gated sawtooth has to produce sound.
        sidProgram("50 POKE S+4,33");
        std::vector<int16_t> buf(16000);
        g_sid.render(buf.data(), int(buf.size()));
        long peak = 0;
        for (int16_t v : buf) peak = std::max<long>(peak, std::abs(long(v)));
        if (peak > 1000) ++passN;
        else { ++failN; std::printf("FAIL  SID: gated voice is silent (peak %ld)\n", peak); }

        // active() has to hold the instant the gate opens, before a single
        // sample exists: the app feeds the speaker only while it does, so a
        // false here would mean the attack never gets a chance to rise.
        sidProgram("50 POKE S+4,33");
        if (g_sid.active()) ++passN;
        else { ++failN; std::printf("FAIL  SID: gate open but the chip reports idle\n"); }

        // Frequency: POKE 25,177 is 6577, which is 6577 * 985248 / 2^24 Hz.
        int crossings = 0;
        for (std::size_t i = 1; i < buf.size(); ++i)
            if (buf[i - 1] < 0 && buf[i] >= 0) ++crossings;
        const double want = 6577.0 * 985248.0 / 16777216.0;   // ~386 Hz
        if (std::abs(crossings - want) < want * 0.03) ++passN;
        else { ++failN; std::printf("FAIL  SID: pitch %d Hz, expected %.0f\n", crossings, want); }

        // Volume 0 must silence the chip outright.
        sidProgram("50 POKE S+4,33 : POKE S+24,0");
        g_sid.render(buf.data(), int(buf.size()));
        peak = 0;
        for (int16_t v : buf) peak = std::max<long>(peak, std::abs(long(v)));
        if (peak == 0) ++passN;
        else { ++failN; std::printf("FAIL  SID: volume 0 still sounds (peak %ld)\n", peak); }

        // Closing the gate has to fade the voice out, not cut it dead.
        sidProgram("50 POKE S+4,33");
        g_sid.render(buf.data(), 4000);
        basic.reset();
        g_sid.write(4, 32);                    // gate off, sawtooth still selected
        g_sid.render(buf.data(), 16000);       // release runs 3x the 2 ms attack
        peak = 0;
        for (std::size_t i = 8000; i < buf.size(); ++i) peak = std::max<long>(peak, std::abs(long(buf[i])));
        if (peak == 0) ++passN;
        else { ++failN; std::printf("FAIL  SID: release never reaches silence (peak %ld)\n", peak); }

        // Every waveform has to make sound on its own.
        {
            const struct { uint8_t bit; const char* name; } waves[] = {
                {0x10, "triangle"}, {0x20, "sawtooth"}, {0x40, "pulse"}, {0x80, "noise"},
            };
            for (const auto& w : waves) {
                g_sid.reset(); g_sid.setSampleRate(16000);
                g_sid.write(24, 15);                 // volume
                g_sid.write(6, 0xF0);                // full sustain
                g_sid.write(3, 0x08);                // half pulse width
                g_sid.write(0, 177); g_sid.write(1, 25);
                g_sid.write(4, uint8_t(w.bit | 1));  // waveform + gate
                g_sid.render(buf.data(), 8000);
                long p2 = 0;
                for (int16_t v : buf) p2 = std::max<long>(p2, std::abs(long(v)));
                if (p2 > 500) ++passN;
                else { ++failN; std::printf("FAIL  SID: %s is silent (peak %ld)\n", w.name, p2); }
            }
        }

        // Sustain level has to scale the held note.
        {
            auto heldPeak = [&](uint8_t sustainNibble) {
                g_sid.reset(); g_sid.setSampleRate(16000);
                g_sid.write(24, 15);
                g_sid.write(5, 0x00);                        // instant attack/decay
                g_sid.write(6, uint8_t(sustainNibble << 4));
                g_sid.write(0, 177); g_sid.write(1, 25);
                g_sid.write(4, 0x21);
                g_sid.render(buf.data(), 8000);
                long q = 0;
                for (std::size_t i = 4000; i < 8000; ++i) q = std::max<long>(q, std::abs(long(buf[i])));
                return q;
            };
            const long full = heldPeak(15), half = heldPeak(7), none = heldPeak(0);
            if (full > half && half > none && none == 0) ++passN;
            else { ++failN; std::printf("FAIL  SID sustain: full=%ld half=%ld none=%ld\n",
                                        full, half, none); }
        }

        // The oscillator-3 tap at 54299 is how listings read noise back.
        g_sid.reset(); g_sid.setSampleRate(16000);
        g_sid.write(14, 100); g_sid.write(15, 30); g_sid.write(18, 0x81);  // v3 noise, gated
        g_sid.render(buf.data(), 2000);
        const uint8_t tap = g_sid.read(27);
        g_sid.render(buf.data(), 2000);
        if (tap != g_sid.read(27)) ++passN;
        else { ++failN; std::printf("FAIL  SID: osc3 tap never changes\n"); }

        // The beginner's listing that leaves the envelope untouched: attack and
        // decay are 0 and so is sustain, so the note is gone within
        // milliseconds even though the gate stays open. The chip has to report
        // itself idle afterwards — on the real machine that program is silent,
        // and here a busy chip would leave the amplifier powered for nothing.
        {
            basic.reset(); scr.reset(); g_sid.reset(); g_sid.setSampleRate(16000);
            typeEnter("10 S=54272");
            typeEnter("20 POKE S+24,15");
            typeEnter("30 POKE S+1,20 : POKE S,100");
            typeEnter("40 POKE S+4,33");
            typeEnter("RUN");
            g_sid.render(buf.data(), int(buf.size()));      // one second
            long tail = 0;
            for (std::size_t i = buf.size() / 2; i < buf.size(); ++i)
                tail = std::max<long>(tail, std::abs(long(buf[i])));
            if (tail == 0 && !g_sid.active()) ++passN;
            else { ++failN; std::printf("FAIL  SID: sustain 0 stays busy (tail %ld, active %d)\n",
                                        tail, int(g_sid.active())); }
        }

        // Hard sync resets the slave once, on the exact sample the master's
        // MSB rises. Voice 3 syncs to voice 2; the osc-3 tap at 54299 exposes
        // the phase, which must match the documented arithmetic (inc = freq *
        // clock / rate, phase restarts at the rise). Pinning the slave for a
        // stretch of samples — the easy mistake — lands on a different byte.
        {
            g_sid.reset(); g_sid.setSampleRate(16000);
            const int FM = 2000, FS = 7000;                       // odd ratio on purpose
            g_sid.write(7,  FM & 0xFF); g_sid.write(8,  FM >> 8); // master: voice 2
            g_sid.write(14, FS & 0xFF); g_sid.write(15, FS >> 8); // slave: voice 3
            g_sid.write(18, 0x02);                                // sync, oscillator only
            const int N = 1000;
            g_sid.render(buf.data(), N);

            const uint32_t incM = uint32_t((uint64_t(FM) * Sid::CLOCK) / 16000u);
            const uint32_t incS = uint32_t((uint64_t(FS) * Sid::CLOCK) / 16000u);
            uint32_t pm = 0; int lastRise = 0;
            for (int n = 1; n <= N; ++n) {
                const uint32_t before = pm;
                pm = (pm + incM) & 0xFFFFFF;
                if (!(before & 0x800000) && (pm & 0x800000)) lastRise = n;
            }
            const uint32_t wantPhase = (uint32_t(N - lastRise) * incS) & 0xFFFFFF;
            const uint8_t wantTap = uint8_t(wantPhase >> 16);
            const uint8_t gotTap  = g_sid.read(27);
            if (gotTap == wantTap) ++passN;
            else { ++failN; std::printf("FAIL  SID sync: osc3 tap %d, expected %d\n", gotTap, wantTap); }
        }

        // The release is the chip's slowing ladder, not a straight line: with
        // release 4 (~114 ms nominal) a linear fall is already silent right
        // after the nominal time — the ladder still sounds there, and only the
        // long tail dies away.
        {
            g_sid.reset(); g_sid.setSampleRate(16000);
            g_sid.write(24, 15);
            g_sid.write(0, 100); g_sid.write(1, 20);
            g_sid.write(5, 0x00); g_sid.write(6, 0xF4);   // sustain 15, release 4
            g_sid.write(4, 0x21);                          // saw + gate
            g_sid.render(buf.data(), 3200);                // settle at full level
            g_sid.write(4, 0x20);                          // gate off
            g_sid.render(buf.data(), 2400);                // 150 ms: linear would be gone
            long after = 0;
            g_sid.render(buf.data(), 800);
            for (int n = 0; n < 800; ++n) after = std::max<long>(after, std::abs(long(buf[n])));
            g_sid.render(buf.data(), 16000);               // a second later: truly out
            long relTail = 0;
            for (int n = 8000; n < 16000; ++n) relTail = std::max<long>(relTail, std::abs(long(buf[n])));
            if (after > 150 && relTail == 0) ++passN;
            else { ++failN; std::printf("FAIL  SID release curve: after-nominal %ld, tail %ld\n",
                                        after, relTail); }
        }

        // Outside attack the envelope only falls: raising sustain mid-note
        // must not lift the level, lowering it drags the voice down.
        {
            g_sid.reset(); g_sid.setSampleRate(16000);
            g_sid.write(24, 15);
            g_sid.write(0, 100); g_sid.write(1, 20);
            g_sid.write(5, 0x08);                 // attack 0, decay 8
            g_sid.write(6, 0x80);                 // sustain 8
            g_sid.write(4, 0x21);
            g_sid.render(buf.data(), 9600);       // settled at 8/15 of full
            auto peakNext = [&](int samples) {
                long p = 0;
                while (samples > 0) {
                    const int n = samples > int(buf.size()) ? int(buf.size()) : samples;
                    g_sid.render(buf.data(), n);
                    for (int k = 0; k < n; ++k) p = std::max<long>(p, std::abs(long(buf[k])));
                    samples -= n;
                }
                return p;
            };
            const long at8 = peakNext(1600);
            g_sid.write(6, 0xF0);                 // try to RAISE sustain to 15
            const long raised = peakNext(1600);
            g_sid.write(6, 0x20);                 // lower it to 2
            peakNext(8000);                        // give it time to fall
            const long lowered = peakNext(1600);
            if (raised <= at8 + at8 / 8 && lowered < at8 / 2) ++passN;
            else { ++failN; std::printf("FAIL  SID sustain: at8=%ld raised=%ld lowered=%ld\n",
                                        at8, raised, lowered); }
        }
    }

    // --- VIC-II sprites ----------------------------------------------------
    {
        // Set sprite 0 up the way a listing does: a solid 24x21 block poked
        // into memory at 832, the pointer aimed at it, a colour, a position and
        // the enable bit. All of it goes through POKE and the address router.
        auto setup = []() {
            basic.reset(); scr.reset(); g_spr.reset();
            typeEnter("FOR I=0 TO 62:POKE 832+I,255:NEXT");   // solid shape at 832
            typeEnter("POKE 2040,13");                         // sprite 0 -> 13*64
            typeEnter("POKE 53287,1");                         // colour white
            typeEnter("POKE 53248,100:POKE 53249,100");        // X=100, Y=100
            typeEnter("POKE 53269,1");                          // enable sprite 0
        };
        auto peek = [](const char* addr) {
            scr.reset();
            typeEnter(std::string("PRINT PEEK(") + addr + ")");
            char b[81]; scr.readLine(1, b, sizeof b); return trim(b);
        };

        setup();
        if (peek("53269") == "1") ++passN;
        else { ++failN; std::printf("FAIL  sprite: enable does not read back\n"); }

        // A solid pixel decodes to the sprite's colour; the shape is 24x21, so
        // column 24 / row 21 are already outside it.
        if (g_spr.pixelColor(0, basic.ram(), 0, 0) == 1 &&
            g_spr.pixelColor(0, basic.ram(), 23, 20) == 1 &&
            g_spr.pixelColor(0, basic.ram(), 24, 0) == -1) ++passN;
        else { ++failN; std::printf("FAIL  sprite: hires pixel decode\n"); }

        // The 9th X bit at 53264 lifts the position past 255.
        setup();
        typeEnter("POKE 53248,44:POKE 53264,1");
        if (g_spr.posX(0) == 300) ++passN;
        else { ++failN; std::printf("FAIL  sprite: X MSB, got %d\n", g_spr.posX(0)); }

        // Two solid sprites on the same spot collide; 53278 names both, and a
        // second read comes back clear — the chip empties it on read.
        setup();
        typeEnter("POKE 2041,13");                    // sprite 1 shares the shape
        typeEnter("POKE 53250,100:POKE 53251,100");   // and the position
        typeEnter("POKE 53269,3");                     // enable 0 and 1
        g_spr.updateCollisions(basic.ram(), scr.fgMask());
        const std::string hit = peek("53278"), again = peek("53278");
        if (hit == "3" && again == "0") ++passN;
        else { ++failN; std::printf("FAIL  sprite collision: [%s] then [%s], want [3] [0]\n",
                                    hit.c_str(), again.c_str()); }

        // Move sprite 1 clear of sprite 0 (24 px wide): no overlap, no hit.
        setup();
        typeEnter("POKE 2041,13:POKE 53269,3");
        typeEnter("POKE 53250,140:POKE 53251,100");   // 40 px to the right
        g_spr.updateCollisions(basic.ram(), scr.fgMask());
        if (peek("53278") == "0") ++passN;
        else { ++failN; std::printf("FAIL  sprite: apart but still colliding\n"); }

        // X-expand doubles sprite 0 to 48 px wide, which now reaches sprite 1.
        typeEnter("POKE 53277,1");                     // expand sprite 0 in X
        g_spr.updateCollisions(basic.ram(), scr.fgMask());
        if (peek("53278") == "3") ++passN;
        else { ++failN; std::printf("FAIL  sprite: X-expand does not extend collision\n"); }

        // Multicolour reads the shape as bit pairs: %01 -> 53285, %11 -> 53286,
        // %10 -> the sprite's own colour, %00 transparent.
        setup();
        typeEnter("POKE 832,27");        // row 0 byte 0 = 00 01 10 11 (pairs)
        typeEnter("POKE 53276,1");       // sprite 0 multicolour
        typeEnter("POKE 53285,5:POKE 53286,6");  // mc0=5, mc1=6 (colour is 1)
        if (g_spr.pixelColor(0, basic.ram(), 0, 0) == -1 &&   // %00 transparent
            g_spr.pixelColor(0, basic.ram(), 2, 0) == 5  &&   // %01 -> mc0
            g_spr.pixelColor(0, basic.ram(), 4, 0) == 1  &&   // %10 -> colour
            g_spr.pixelColor(0, basic.ram(), 6, 0) == 6) ++passN;   // %11 -> mc1
        else { ++failN; std::printf("FAIL  sprite: multicolour decode\n"); }

        // The renderer paints the shape: a solid on-screen sprite is exactly
        // 24x21 = 504 pixels of its colour, and none once it is disabled.
        setup();
        scr.reset();
        static std::vector<uint16_t> buf(board::display::WIDTH * board::display::HEIGHT);
        board::gfx::Surface surf{buf.data(), board::display::WIDTH, board::display::HEIGHT};
        scr.render(surf, false);
        scr.renderSprites(surf, g_spr, basic.ram());
        auto countColor = [&](uint16_t c) {
            int n = 0; for (uint16_t p : buf) if (p == c) ++n; return n; };
        const int white = countColor(PALETTE[1]);
        typeEnter("POKE 53269,0");        // disable
        scr.reset(); scr.render(surf, false); scr.renderSprites(surf, g_spr, basic.ram());
        const int whiteOff = countColor(PALETTE[1]);
        if (white == 504 && whiteOff == 0) ++passN;
        else { ++failN; std::printf("FAIL  sprite render: on=%d (want 504) off=%d (want 0)\n",
                                    white, whiteOff); }
    }

    // --- VIC-II graphics: charset, bitmap, priority, collisions ------------
    {
        static std::vector<uint16_t> buf(board::display::WIDTH * board::display::HEIGHT);
        board::gfx::Surface surf{buf.data(), board::display::WIDTH, board::display::HEIGHT};
        // Portrait 1:1 rendering: the 320x200 field starts at this pixel.
        const int OX = (board::display::WIDTH - 320) / 2;
        const int OY = (board::display::HEIGHT - 200) / 2;
        auto pixel = [&](int x, int y) { return buf[(OY + y) * board::display::WIDTH + (OX + x)]; };
        auto renderAll = [&]() {
            scr.render(surf, false, &g_spr, basic.ram());
            g_spr.updateCollisions(basic.ram(), scr.fgMask());
            scr.renderSprites(surf, g_spr, basic.ram());
        };
        auto peek = [](int addr) {
            scr.reset();
            typeEnter("PRINT PEEK(" + std::to_string(addr) + ")");
            char b[81]; scr.readLine(1, b, sizeof b); return trim(b);
        };

        // The character generator "ROM" is readable at 4096: the classic copy
        // loop works without any bank-switching. The upper half is reversed.
        checkProg("charset rom is readable at 4096",
            {"10 PRINT PEEK(4096);PEEK(5120)"}, {"120  135"});

        // With the default $D018 the ROM font draws: 'A' (code 1) puts its top
        // glyph row into the foreground mask of cell (0,0).
        basic.reset(); scr.reset(); g_spr.reset();
        typeEnter("POKE 1024,1");
        scr.render(surf, false, &g_spr, basic.ram());
        if (scr.fgMask()[0] == 0x30) ++passN;
        else { ++failN; std::printf("FAIL  charset: rom glyph mask %02X, want 30\n", scr.fgMask()[0]); }

        // A custom set: point $D018 at 12288, poke a striped glyph for code 1,
        // and the same cell now draws the poked shape instead of the ROM 'A'.
        typeEnter("POKE 53272,(PEEK(53272)AND240)OR12");
        typeEnter("FOR I=0 TO 7:POKE 12288+8+I,170:NEXT");
        scr.render(surf, false, &g_spr, basic.ram());
        bool striped = true;
        for (int r = 0; r < 8; ++r) striped &= scr.fgMask()[r * 40] == 170;
        if (striped) ++passN;
        else { ++failN; std::printf("FAIL  charset: custom glyph not drawn from ram\n"); }

        // Back to the ROM pointer: the poked set stays in RAM but stops showing.
        typeEnter("POKE 53272,21");
        scr.render(surf, false, &g_spr, basic.ram());
        if (scr.fgMask()[0] == 0x30) ++passN;
        else { ++failN; std::printf("FAIL  charset: rom pointer does not restore the font\n"); }

        // DEN off blanks the display to the border; nothing is foreground.
        typeEnter("POKE 53265,PEEK(53265)AND239");
        scr.render(surf, false, &g_spr, basic.ram());
        bool blank = pixel(4, 4) == buf[0];   // window pixel equals border pixel
        for (int i = 0; i < 200 * 40; ++i) blank &= scr.fgMask()[i] == 0;
        if (blank) ++passN;
        else { ++failN; std::printf("FAIL  vic: DEN off does not blank the screen\n"); }
        typeEnter("POKE 53265,PEEK(53265)OR16");

        // Hi-res bitmap: the classic recipe — mode on, bitmap at 8192, plot
        // X=100/Y=50 with the POKE formula, colours from the screen matrix.
        basic.reset(); scr.reset(); g_spr.reset();
        typeEnter("POKE 53265,PEEK(53265)OR32");
        typeEnter("POKE 53272,PEEK(53272)OR8");
        typeEnter("X=100:Y=50");
        typeEnter("BY=8192+320*INT(Y/8)+8*INT(X/8)+(Y AND 7)");
        typeEnter("POKE BY,PEEK(BY) OR 2^(7-(X AND 7))");
        typeEnter("POKE 1024+40*INT(Y/8)+INT(X/8),16*1+6");   // white on blue
        renderAll();
        const bool maskOk = scr.fgMask()[50 * 40 + 12] == 0x08;
        if (maskOk && pixel(100, 50) == PALETTE[1] && pixel(101, 50) == PALETTE[6]) ++passN;
        else { ++failN; std::printf("FAIL  bitmap: plot mask %02X px %04X bg %04X\n",
                                    scr.fgMask()[50 * 40 + 12], pixel(100, 50), pixel(101, 50)); }

        // Multicolour bitmap: pairs %11 -> colour RAM, %01 -> colour-cell high
        // nibble, %00 -> the background register; only %10/%11 are foreground.
        typeEnter("POKE 53270,PEEK(53270)OR16");
        typeEnter("POKE 53281,6");           // %00 pairs show this background
        typeEnter("POKE 8192,196");          // pairs 11 00 01 00
        typeEnter("POKE 1024,16*3+4");       // %01 -> cyan
        typeEnter("POKE 55296,7");           // %11 -> yellow
        renderAll();
        if (pixel(0, 0) == PALETTE[7] && pixel(2, 0) == PALETTE[6] &&
            pixel(4, 0) == PALETTE[3] && scr.fgMask()[0] == 0xC0) ++passN;
        else { ++failN; std::printf("FAIL  mc bitmap: px %04X %04X %04X mask %02X\n",
                                    pixel(0, 0), pixel(2, 0), pixel(4, 0), scr.fgMask()[0]); }

        // Sprite behind the text ($D01B): its pixels lose against glyph
        // foreground and win over background — and hitting that foreground
        // sets the sprite/background latch at 53279, which clears on read.
        basic.reset(); scr.reset(); g_spr.reset();
        typeEnter("FOR I=0 TO 62:POKE 832+I,255:NEXT");
        typeEnter("POKE 2040,13:POKE 53287,1");
        typeEnter("POKE 53248,24:POKE 53249,50:POKE 53269,1");
        typeEnter("POKE 53275,1");                     // sprite 0 behind
        // The solid block goes in directly — typing a POKE would echo its own
        // text into the very cells the check reads.
        scr.reset(); scr.pokeScreen(0, 160); scr.pokeColor(0, 14);   // block at (0,0)
        renderAll();
        const bool behindOk = pixel(2, 2) == PALETTE[14] && pixel(10, 2) == PALETTE[1];
        const std::string sb = peek(53279), sbAgain = peek(53279);
        typeEnter("POKE 53275,0");
        scr.reset(); scr.pokeScreen(0, 160); scr.pokeColor(0, 14);
        renderAll();
        const bool frontOk = pixel(2, 2) == PALETTE[1];
        if (behindOk && frontOk && sb == "1" && sbAgain == "0") ++passN;
        else { ++failN; std::printf("FAIL  sprite priority/sb: behind=%d front=%d sb=[%s][%s]\n",
                                    int(behindOk), int(frontOk), sb.c_str(), sbAgain.c_str()); }

        // Over empty background there is no hit. The front render above hit
        // the block, so drain that latch before looking at the clean frame.
        peek(53279);
        typeEnter("POKE 53248,200");                   // move clear of the block
        scr.reset();
        renderAll();
        if (peek(53279) == "0") ++passN;
        else { ++failN; std::printf("FAIL  sprite/bg: hit reported over empty screen\n"); }

        // Multicolour %01 looks solid but is background to the collision
        // logic; %10 of the same shape collides.
        typeEnter("POKE 53248,24:POKE 53276,1");
        typeEnter("FOR I=0 TO 62:POKE 832+I,85:NEXT");    // all %01 pairs
        scr.reset(); scr.pokeScreen(0, 160);              // fg under the sprite
        renderAll();
        const std::string mc01 = peek(53279);
        typeEnter("FOR I=0 TO 62:POKE 832+I,170:NEXT");   // all %10 pairs
        scr.reset(); scr.pokeScreen(0, 160); scr.pokeColor(0, 14);
        renderAll();
        const std::string mc10 = peek(53279);
        if (mc01 == "0" && mc10 == "1") ++passN;
        else { ++failN; std::printf("FAIL  mc collision rule: %%01=[%s] %%10=[%s], want 0 / 1\n",
                                    mc01.c_str(), mc10.c_str()); }

        // The raster register cannot sit still, or WAIT 53266 would hang.
        if (peek(53266) != peek(53266)) ++passN;
        else { ++failN; std::printf("FAIL  vic: raster register is frozen\n"); }
    }

    // --- SID filter ---------------------------------------------------------
    {
        std::vector<int16_t> fbuf(4000);
        // One gated sawtooth on voice 1, rendered past the attack; the last
        // half of the block is the settled level the checks compare.
        auto peakWith = [&](std::initializer_list<std::pair<int, uint8_t>> regs) {
            g_sid.reset(); g_sid.setSampleRate(16000);
            g_sid.write(24, 15);
            g_sid.write(0, 0x40); g_sid.write(1, 0x1F);   // ~470 Hz
            g_sid.write(5, 0x00); g_sid.write(6, 0xF0);   // full sustain
            for (auto& rv : regs) g_sid.write(rv.first, rv.second);
            g_sid.write(4, 0x21);
            g_sid.render(fbuf.data(), int(fbuf.size()));
            long p = 0;
            for (std::size_t i = fbuf.size() / 2; i < fbuf.size(); ++i)
                p = std::max<long>(p, std::abs(long(fbuf[i])));
            return p;
        };

        const long plain    = peakWith({});
        const long lpOpen   = peakWith({{23, 1}, {21, 7}, {22, 255}, {24, 0x1F}});
        const long lpClosed = peakWith({{23, 1}, {21, 0}, {22, 0},   {24, 0x1F}});
        if (lpOpen > plain / 2 && lpClosed < plain / 4 && lpClosed < lpOpen / 3) ++passN;
        else { ++failN; std::printf("FAIL  filter lp: plain=%ld open=%ld closed=%ld\n",
                                    plain, lpOpen, lpClosed); }

        // High-pass mirrors it: wide open (cutoff at the bottom) passes the
        // tone, cutoff far above it strangles it.
        // A sawtooth keeps real energy in its upper harmonics, so even a
        // cutoff far above the fundamental leaves audible treble — the check
        // is a clear drop, not silence.
        const long hpLow  = peakWith({{23, 1}, {21, 0}, {22, 0},   {24, 0x4F}});
        const long hpHigh = peakWith({{23, 1}, {21, 7}, {22, 255}, {24, 0x4F}});
        if (hpLow > plain * 2 / 3 && hpHigh < hpLow / 2) ++passN;
        else { ++failN; std::printf("FAIL  filter hp: plain=%ld low=%ld high=%ld\n",
                                    plain, hpLow, hpHigh); }

        // A voice routed into the filter with no mode selected disappears —
        // and $D418 bit 7 mutes an unfiltered voice 3 outright.
        const long routedNoMode = peakWith({{23, 1}});
        g_sid.reset(); g_sid.setSampleRate(16000);
        g_sid.write(24, 0x8F);                            // volume 15, voice 3 off
        g_sid.write(14, 0x40); g_sid.write(15, 0x1F);
        g_sid.write(19, 0x00); g_sid.write(20, 0xF0);
        g_sid.write(18, 0x21);
        g_sid.render(fbuf.data(), int(fbuf.size()));
        long v3off = 0;
        for (std::size_t i = fbuf.size() / 2; i < fbuf.size(); ++i)
            v3off = std::max<long>(v3off, std::abs(long(fbuf[i])));
        g_sid.write(24, 0x0F);                            // bit 7 back off
        g_sid.render(fbuf.data(), int(fbuf.size()));
        long v3on = 0;
        for (std::size_t i = fbuf.size() / 2; i < fbuf.size(); ++i)
            v3on = std::max<long>(v3on, std::abs(long(fbuf[i])));
        if (routedNoMode == 0 && v3off == 0 && v3on > 1000) ++passN;
        else { ++failN; std::printf("FAIL  filter routing: nomode=%ld v3off=%ld v3on=%ld\n",
                                    routedNoMode, v3off, v3on); }
    }

    // The user's multiplication trainer, exactly as pasted — the length of
    // line 50 once broke the serial path, so it stays here verbatim. RND is
    // seeded deterministically: the first round asks 2*10, the second 5*6.
    {
        basic.reset(); scr.reset(); g_spr.reset();
        const char* trainer[] = {
            "10 A=INT(RND(1)*10)+1",
            "20 B=INT(RND(1)*10)+1",
            "30 PRINT A;\" * \";B;\" = \";",
            "40 INPUT R",
            "50 IF R=A*B THEN PRINT \"RICHTIG!\":GOTO 70",
            "60 PRINT \"FALSCH. ERGEBNIS IST \";A*B",
            "70 GOTO 10",
        };
        for (const char* l : trainer) typeEnter(l);
        g_in.assign({"20", "0"});                     // right, then wrong
        for (char c : std::string("RUN")) feed((uint8_t)c);
        feed(0x0D);
        int guard = 0;
        while (basic.busy() && guard++ < 100000) {
            if (basic.mode() == Basic::Mode::Input) {
                if (g_in.empty()) break;              // both answers given
                std::string r = g_in.front(); g_in.pop_front();
                for (char c : r) scr.put((uint8_t)c);
                scr.put(0x0D);
                basic.provideInput(r.c_str());
            }
            basic.poll();
        }
        // Leave the endless loop cleanly: answer the pending INPUT once more,
        // then break out of Running — poll only honours the break flag there.
        if (basic.mode() == Basic::Mode::Input) basic.provideInput("0");
        basic.breakRun();
        basic.poll();
        std::string all;
        for (int r = 0; r < Screen::ROWS; ++r) {
            char b[81]; scr.readLine(r, b, sizeof b);
            all += b; all += '\n';
        }
        if (all.find("RICHTIG!") != std::string::npos &&
            all.find("FALSCH. ERGEBNIS IST  30") != std::string::npos &&
            all.find("?SYNTAX") == std::string::npos) ++passN;
        else { ++failN; std::printf("FAIL  trainer: screen was\n%s\n", all.c_str()); }
    }

    // --- file I/O: OPEN / CLOSE / PRINT# / INPUT# / GET# / CMD ------------
    checkProg("write a file and read it back",
        {"10 OPEN 1,8,2,\"DATA,S,W\"", "20 PRINT#1,\"HELLO\"", "30 PRINT#1,42",
         "40 CLOSE 1", "50 OPEN 1,8,2,\"DATA,S,R\"", "60 INPUT#1,A$", "70 INPUT#1,B",
         "80 CLOSE 1", "90 PRINT A$;B"}, {"HELLO 42"});

    checkProg("INPUT# splits a line on commas",
        {"10 OPEN 1,8,2,\"CSV,S,W\"", "20 PRINT#1,\"AB,7\"", "30 CLOSE 1",
         "40 OPEN 1,8,2,\"CSV,S,R\"", "50 INPUT#1,A$,B", "60 CLOSE 1",
         "70 PRINT A$;B"}, {"AB 7"});

    checkProg("ST reports end of file",
        {"10 OPEN 1,8,2,\"E,S,W\"", "20 PRINT#1,\"X\"", "30 CLOSE 1",
         "40 OPEN 1,8,2,\"E,S,R\"", "50 INPUT#1,A$", "60 PRINT ST", "70 CLOSE 1"},
        {"64"});

    checkProg("GET# reads one character at a time",
        {"10 OPEN 1,8,2,\"G,S,W\"", "20 PRINT#1,\"AB\"", "30 CLOSE 1",
         "40 OPEN 1,8,2,\"G,S,R\"", "50 GET#1,A$ : GET#1,B$", "60 CLOSE 1",
         "70 PRINT A$;B$"}, {"AB"});

    checkProg("CMD redirects PRINT until CLOSE",
        {"10 OPEN 1,8,2,\"C,S,W\"", "20 CMD 1", "30 PRINT \"INTOFILE\"",
         "40 CLOSE 1", "50 OPEN 1,8,2,\"C,S,R\"", "60 INPUT#1,A$", "70 CLOSE 1",
         "80 PRINT A$"}, {"INTOFILE"});

    checkProg("opening the same file twice",
        {"10 OPEN 1,8,2,\"A,S,W\"", "20 OPEN 1,8,2,\"B,S,W\""}, {"?FILE OPEN ERROR IN 20"});
    checkProg("closing a file that was never open is harmless",
        {"10 CLOSE 7", "20 PRINT \"FINE\""}, {"FINE"});
    checkProg("two files open at once",
        {"10 OPEN 1,8,2,\"P,S,W\" : OPEN 2,8,2,\"Q,S,W\"",
         "20 PRINT#1,\"ONE\" : PRINT#2,\"TWO\"", "30 CLOSE 1 : CLOSE 2",
         "40 OPEN 1,8,2,\"P,S,R\" : OPEN 2,8,2,\"Q,S,R\"",
         "50 INPUT#1,A$ : INPUT#2,B$", "60 CLOSE 1 : CLOSE 2", "70 PRINT A$;B$"},
        {"ONETWO"});
    checkProg("reading past the end yields empty strings",
        {"10 OPEN 1,8,2,\"Z,S,W\"", "20 PRINT#1,\"X\"", "30 CLOSE 1",
         "40 OPEN 1,8,2,\"Z,S,R\"", "50 INPUT#1,A$ : INPUT#1,B$", "60 CLOSE 1",
         "70 PRINT \"[\";A$;\"][\";B$;\"]\""}, {"[X][]"});

    checkProg("reading a file that is not there",
        {"10 OPEN 1,8,2,\"NOPE,S,R\""}, {"?FILE NOT FOUND ERROR IN 10"});
    checkProg("using a file that was never opened",
        {"10 PRINT#3,\"X\""}, {"?FILE NOT OPEN ERROR IN 10"});

    // A real listing writes SAVE "NAME",8 — the device number must be accepted
    // and ignored, not treated as a syntax error.
    {
        g_files.f.clear();
        auto out = seq({"10 PRINT 1", "SAVE \"DEV\",8", "DIRECTORY"});
        const bool ok = out.size() >= 2 && out[1].find("\"DEV\"") != std::string::npos;
        if (ok) ++passN;
        else { ++failN; std::printf("FAIL  SAVE with device number  got: ");
               for (auto& x : out) std::printf("[%s] ", x.c_str()); std::printf("\n"); }
    }
    checkSeq("VERIFY confirms a saved program",
        {"10 PRINT 1", "SAVE \"VER\",8", "VERIFY \"VER\",8"}, {"OK"});

    // --- reserved variables and the last functions ------------------------
    basic.setMillis(0);
    checkSeq("TI starts at zero", {"PRINT TI"}, {"0"});
    checkSeq("TI$ starts at midnight", {"PRINT TI$"}, {"000000"});
    checkSeq("TI$ can be set and read back", {"TI$=\"012345\"", "PRINT TI$"}, {"012345"});
    checkSeq("TI counts jiffies from TI$", {"TI$=\"000001\"", "PRINT TI"}, {"60"});
    checkSeq("TI is read-only", {"TI=5"}, {"?SYNTAX ERROR"});
    checkSeq("ST reads as zero", {"PRINT ST"}, {"0"});
    checkSeq("POS reports the cursor column", {"PRINT \"ABC\";POS(0)"}, {"ABC 3"});
    // FRE comes back through a signed 16-bit register on the original, so a
    // nearly empty machine reports a negative number and you add 65536.
    checkSeq("FRE reports the signed-16-bit quirk", {"PRINT FRE(0)"}, {"-26625"});
    checkSeq("FRE drops as the program grows",
        {"10 REM 1234567890", "PRINT FRE(0)<-26625"}, {"-1"});
    // WAIT spins until the bit appears; poking it first lets the test finish.
    checkProg("WAIT falls through once the bit is set",
        {"10 POKE 4096,255", "20 WAIT 4096,1", "30 PRINT \"PAST\""}, {"PAST"});

    // --- PETSCII control codes inside PRINT ---------------------------
    // These are checked against the screen directly: CLR wipes the echoed
    // command line, so there is no output text left to compare.
    auto ctrl = [&](const char* name, const char* cmd, bool (*check)()) {
        basic.reset(); scr.reset();
        typeEnter(cmd);
        if (check()) ++passN;
        else { ++failN; std::printf("FAIL  control code: %s\n", name); }
    };
    ctrl("CHR$(147) clears the screen", "PRINT CHR$(147);\"X\"",
         []{ char b[81]; scr.readLine(0, b, sizeof b); return std::string(b) == "X"; });
    ctrl("CHR$(28) switches to red", "PRINT CHR$(147);CHR$(28);\"R\"",
         []{ return scr.peekColor(0) == 2 && scr.peekScreen(0) == 18; });
    ctrl("CHR$(158) switches to yellow", "PRINT CHR$(147);CHR$(158);\"Y\"",
         []{ return scr.peekColor(0) == 7; });
    ctrl("CHR$(18) turns on reverse video", "PRINT CHR$(147);CHR$(18);\"A\"",
         []{ return scr.peekScreen(0) == (1 | 0x80); });
    ctrl("CHR$(146) turns reverse off again", "PRINT CHR$(147);CHR$(18);CHR$(146);\"A\"",
         []{ return scr.peekScreen(0) == 1; });
    ctrl("CHR$(19) sends the cursor home", "PRINT CHR$(147);\"AB\";CHR$(19);\"C\"",
         []{ return scr.peekScreen(0) == 3 && scr.peekScreen(1) == 2; });
    ctrl("CHR$(17) moves the cursor down", "PRINT CHR$(147);CHR$(17);\"D\"",
         []{ return scr.peekScreen(40) == 4; });
    ctrl("colour survives into the next PRINT", "PRINT CHR$(147);CHR$(30);:PRINT \"G\"",
         []{ return scr.peekColor(0) == 5; });

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
