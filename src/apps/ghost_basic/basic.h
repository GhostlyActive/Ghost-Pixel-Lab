// A BASIC V2-flavoured interpreter. Stores program lines by number, runs them,
// and executes commands typed in direct mode. Output goes to the Screen.
//
// Execution is *stepped*: poll() runs a bounded number of statements per call
// so the frame loop never freezes on a long loop, RUN/STOP can break, and
// INPUT can suspend the program until the user submits a line. This is what
// lets it host real interactive programs on a per-frame device.
//
// Source-storing, not tokenising: each line keeps its upper-cased source text,
// which LIST prints back. A byte-exact tokeniser is a later authenticity pass.
#pragma once

#include "screen.h"
#include "sid.h"
#include "vic.h"

#include <cstdint>
#include <string>
#include <vector>

namespace apps::ghost {

// Program storage for SAVE / LOAD / DIRECTORY. Injected by the host app so
// this file keeps no board or Arduino dependency and stays testable off-device.
struct Files {
    struct Entry { std::string name; uint32_t size; };
    virtual ~Files() = default;
    virtual bool save(const char* name, const std::string& text) = 0;
    virtual bool load(const char* name, std::string& out) = 0;
    virtual bool list(std::vector<Entry>& out) = 0;
    virtual bool remove(const char* name) = 0;
    virtual uint32_t freeBytes() { return 0; }
    // Two-character volume id shown by DIRECTORY, like a 1541 disk id. The app
    // uses it to say which medium is actually being written to.
    virtual const char* volumeId() { return "GP"; }
};

class Basic {
public:
    explicit Basic(Screen& screen);
    ~Basic();

    void setFiles(Files* f) { files_ = f; }
    // The sound chip lives in the app so it can also drive the speaker; POKEs
    // to 54272.. are routed here.
    void setSid(Sid* s) { sid_ = s; }
    // The VIC-II lives in the app so it can draw the picture; POKEs to 53248..
    // are routed here. Shapes, charsets and the bitmap live in ram_, which the
    // renderer reads back through ram().
    void setVic(Vic* s) { vic_ = s; }
    const uint8_t* ram() const { return ram_; }

    enum class Mode { Idle, Running, Input };

    void reset();                     // NEW: wipe program + variables

    Mode mode() const { return mode_; }
    bool busy() const { return mode_ != Mode::Idle; }

    void execLine(const char* text);  // Idle: store a line, or run a direct command
    void provideInput(const char* text); // Input: the user submitted a line
    void pushKey(uint8_t petscii);    // Running: feed the GET buffer
    void breakRun();                  // RUN/STOP pressed
    // Drives TI / TI$. The app feeds it millis() once per frame; the host
    // tests set it by hand, which keeps this file free of any clock source.
    void setMillis(uint32_t ms) { nowMs_ = ms; }
    void poll();                      // advance one frame's worth of work

private:
    // ---- value ---------------------------------------------------------
    struct Value {
        bool        isStr = false;
        double      num   = 0;
        std::string str;
        static Value number(double n) { Value v; v.num = n; return v; }
        static Value string(std::string s) { Value v; v.isStr = true; v.str = std::move(s); return v; }
    };

    // ---- storage -------------------------------------------------------
    // A bare name is floating point, a trailing $ makes it a string and a
    // trailing % an integer — the three flavours the original knows. Name,
    // flavour and array-ness together identify a variable, so A, A$, A% and
    // A(0) can all coexist.
    enum VarType : uint8_t { VT_NUM = 0, VT_STR = 1, VT_INT = 2 };

    struct Line { int num; std::string src; };
    struct Var  { char n0, n1; uint8_t type; Value val; };
    struct Arr  { char n0, n1; uint8_t type; std::vector<int> dim;
                  std::vector<double> num; std::vector<std::string> s; };
    struct Fn   { char n0, n1; std::string param, expr; };
    struct ForRec { int varIdx; double limit, step; int line; std::size_t pos; };
    struct SubRec { int line; std::size_t pos; };
    struct InTarget { char n0, n1; uint8_t type; };

    // An OPENed logical file. Contents live in one buffer: a read file is
    // slurped on OPEN, a write file is flushed on CLOSE. Programs of the size
    // this machine runs never need streaming, and it keeps the Files interface
    // to four plain methods.
    struct OpenFile {
        int  lf = 0, dev = 8, sa = 0;
        bool write = false, used = false;
        std::string name, buf;
        std::size_t pos = 0;
    };
    static constexpr int MAX_FILES = 8;

    // ---- program management -------------------------------------------
    void storeLine(int num, const std::string& src);
    int  findLine(int num) const;
    void listProgram(int from, int to);

    // ---- execution -----------------------------------------------------
    void runProgram();
    void loadLine();
    void execStatement();
    void finishRun();                 // print READY. and go Idle
    void doBreak();                   // STOP / RUN-STOP: print BREAK, save CONT

    void stPrint(); void stIf(); void stFor(); void stNext();
    // One target of a NEXT (an explicit variable, or the innermost loop when
    // unnamed). Returns true if the loop iterated and control jumped back into
    // it, which ends a comma list early — NEXT J,I only reaches I once J is done.
    bool nextOne(bool named, char n0, char n1);
    void stGoto(); void stGosub(); void stReturn(); void stOn();
    void stInput(); void stGet(); void stDim(); void stRead();
    void stPoke(); void stDef(); void stAssign();
    void stSave(); void stLoad(); void stScratch(); void directory();
    void stWait();
    void stOpen(); void stClose(); void stPrintFile(); void stInputFile();
    void stGetFile(); void stCmd(); void stVerify();
    OpenFile* findFile(int lf);
    void      skipDeviceSuffix();     // the ",8" / ",8,1" after a file name
    int       outColumn();            // column of whatever output is active
    bool parseFileName(std::string& out);

    // ---- expressions ---------------------------------------------------
    Value parseExpr();
    Value parseOr();  Value parseAnd(); Value parseNot();
    Value parseCmp(); Value parseAdd(); Value parseMul();
    Value parsePow(); Value parseUnary(); Value parsePrimary();
    Value callFunction(const std::string& name);
    Value callFn(const std::string& name);
    static bool isFunction(const std::string& name);

    // ---- variables & arrays -------------------------------------------
    Var*  findVar(char a, char b, uint8_t type);
    Value getVar(const std::string& name, uint8_t type);
    void  setVar(const std::string& name, uint8_t type, const Value& v);
    Arr*  findArr(char a, char b, uint8_t type);
    Arr&  ensureArr(const std::string& name, uint8_t type, int ndims);
    // Fit a value to a variable's flavour: integers truncate toward zero and
    // must stay in -32768..32767, strings and floats pass through.
    Value coerce(const Value& v, uint8_t type, bool lenient);
    std::vector<int> parseIndices();
    int   flatIndex(const Arr& a, const std::vector<int>& idx);
    Value getArr(const std::string& name, uint8_t type);
    void  assignTarget(const Value& v);  // parse a scalar/array lvalue and store
    void  clearVars();

    // ---- reserved variables --------------------------------------------
    long        jiffies() const;          // TI: 1/60 s since the clock was set
    std::string timeString() const;       // TI$: "HHMMSS"
    void        setTimeString(const std::string& hhmmss);
    long        freeBytes() const;        // FRE: what is left of 38911

    // ---- memory map ----------------------------------------------------
    // POKE/PEEK are a real address bus: the screen, colour RAM and the two VIC
    // colour registers live at their original C64 addresses, everything else
    // falls through to plain RAM.
    void    pokeMem(int addr, uint8_t value);
    uint8_t peekMem(int addr) const;

    // ---- DATA / READ ---------------------------------------------------
    bool  readData(std::string& out);
    void  restoreData() { dataLine_ = 0; dataPos_ = 0; dataInStmt_ = false; }

    // ---- lexer ---------------------------------------------------------
    void        skipSpaces();
    bool        atEnd() const;
    char        peek() const;
    bool        matchKw(const char* kw);
    bool        parseLineNumber(int& out);
    std::string parseName(uint8_t& type);

    // ---- output --------------------------------------------------------
    void outChar(char c);
    void outText(const char* s);
    void outText(const std::string& s) { outText(s.c_str()); }
    void outNumber(double v);
    std::string formatNumber(double v);

    // ---- errors --------------------------------------------------------
    void setError(const char* msg);
    void reportError();

    Screen&             screen_;
    std::vector<Line>   lines_;
    std::vector<Var>    vars_;
    std::vector<Arr>    arrays_;
    std::vector<Fn>     fns_;
    std::vector<ForRec> forStack_;
    std::vector<SubRec> gosubStack_;

    std::string directText_;
    const char* src_ = nullptr;
    std::size_t pos_ = 0, len_ = 0;
    std::size_t stmtStart_ = 0;       // where the current statement began
    int  lineIdx_ = -1;               // -1 = direct mode
    Mode mode_    = Mode::Idle;
    const char* err_ = nullptr;

    // INPUT suspension.
    std::vector<InTarget> inTargets_;

    // GET key buffer (keys typed while a program runs).
    std::string getBuf_;

    // RUN/STOP + CONT.
    bool breakReq_  = false;
    int  contLine_  = -1;
    std::size_t contPos_ = 0;
    bool contOk_    = false;

    // DATA cursor.
    int  dataLine_ = 0;
    std::size_t dataPos_ = 0;
    bool dataInStmt_ = false;

    Files*   files_ = nullptr;        // SAVE / LOAD backend, null = no drive
    Sid*     sid_   = nullptr;        // sound chip at 54272, null = silent
    Vic* vic_ = nullptr;              // VIC-II register file at 53248, null = none
    uint8_t* ram_ = nullptr;          // 64K for POKE/PEEK (+ future HW mapping)
    uint32_t rngState_ = 0x1234567u;

    // Open files, PRINT redirection and the I/O status behind ST.
    OpenFile openFiles_[MAX_FILES];
    int printTo_ = -1;     // logical file PRINT writes to, -1 = the screen
    int cmdLf_   = -1;     // CMD redirection, cleared by CLOSE
    int st_      = 0;      // ST: 64 after reading past the end

    // Jiffy clock behind TI / TI$.
    uint32_t nowMs_ = 0;
    long     tiOffset_ = 0;   // jiffies added on top of the running millis()
};

} // namespace apps::ghost
