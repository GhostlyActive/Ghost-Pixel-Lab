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

    enum class Mode { Idle, Running, Input };

    void reset();                     // NEW: wipe program + variables

    Mode mode() const { return mode_; }
    bool busy() const { return mode_ != Mode::Idle; }

    void execLine(const char* text);  // Idle: store a line, or run a direct command
    void provideInput(const char* text); // Input: the user submitted a line
    void pushKey(uint8_t petscii);    // Running: feed the GET buffer
    void breakRun();                  // RUN/STOP pressed
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
    struct Line { int num; std::string src; };
    struct Var  { char n0, n1; bool str; Value val; };
    struct Arr  { char n0, n1; bool str; std::vector<int> dim;
                  std::vector<double> num; std::vector<std::string> s; };
    struct Fn   { char n0, n1; std::string param, expr; };
    struct ForRec { int varIdx; double limit, step; int line; std::size_t pos; };
    struct SubRec { int line; std::size_t pos; };
    struct InTarget { char n0, n1; bool str; };

    // ---- program management -------------------------------------------
    void storeLine(int num, const std::string& src);
    int  findLine(int num) const;
    void listProgram(int from, int to);

    // ---- execution -----------------------------------------------------
    void runProgram();
    void loadLine();
    void execStatement();
    void finishRun();                 // print READY. and go Idle
    void doBreak(bool stopKeyword);   // STOP / RUN-STOP: print BREAK, save CONT

    void stPrint(); void stIf(); void stFor(); void stNext();
    void stGoto(); void stGosub(); void stReturn(); void stOn();
    void stInput(); void stGet(); void stDim(); void stRead();
    void stPoke(); void stDef(); void stAssign(bool letSeen);
    void stSave(); void stLoad(); void stScratch(); void directory();
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
    Var*  findVar(char a, char b, bool str);
    Value getVar(const std::string& name, bool str);
    void  setVar(const std::string& name, bool str, const Value& v);
    Arr*  findArr(char a, char b, bool str);
    Arr&  ensureArr(const std::string& name, bool str, int ndims);
    std::vector<int> parseIndices();
    int   flatIndex(const Arr& a, const std::vector<int>& idx);
    Value getArr(const std::string& name, bool str);
    void  assignTarget(const Value& v);  // parse a scalar/array lvalue and store
    void  clearVars();

    // ---- DATA / READ ---------------------------------------------------
    bool  readData(std::string& out);
    void  restoreData() { dataLine_ = 0; dataPos_ = 0; dataInStmt_ = false; }

    // ---- lexer ---------------------------------------------------------
    void        skipSpaces();
    bool        atEnd() const;
    char        peek() const;
    bool        matchKw(const char* kw);
    bool        parseLineNumber(int& out);
    std::string parseName(bool& isStr);

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
    uint8_t* ram_ = nullptr;          // 64K for POKE/PEEK (+ future HW mapping)
    uint32_t rngState_ = 0x1234567u;
};

} // namespace apps::ghost
