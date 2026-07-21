// Ghost BASIC — the machine half of the interpreter: program storage, the
// stepped run loop, statement dispatch and every statement, DATA/READ, screen
// output and the C64 error messages. Expressions, functions, variables and the
// lexer live in basic_expr.cpp.
#include "basic.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace apps::ghost {

namespace {
constexpr int STEP_BUDGET = 2000;   // statements executed per poll()

std::string upperNoQuotes(const char* s) {
    std::string out;
    bool inQuote = false;
    for (; *s; ++s) {
        char c = *s;
        if (c == '"') inQuote = !inQuote;
        if (!inQuote && c >= 'a' && c <= 'z') c = char(c - 32);
        out += c;
    }
    return out;
}
}

Basic::Basic(Screen& screen) : screen_(screen) {
    ram_ = (uint8_t*)std::calloc(65536, 1);
}
Basic::~Basic() { std::free(ram_); }

// ---------------------------------------------------------------------------
// program lifecycle
// ---------------------------------------------------------------------------

void Basic::reset() {
    lines_.clear();
    clearVars();
    mode_ = Mode::Idle;
    contOk_ = false;
}

void Basic::clearVars() {
    vars_.clear();
    arrays_.clear();
    fns_.clear();
    forStack_.clear();
    gosubStack_.clear();
    getBuf_.clear();
    for (auto& f : openFiles_) f.used = false;
    if (sid_) sid_->reset();
    printTo_ = -1; cmdLf_ = -1; st_ = 0;
    restoreData();
    if (ram_) std::memset(ram_, 0, 65536);
}

int Basic::findLine(int num) const {
    for (std::size_t i = 0; i < lines_.size(); ++i)
        if (lines_[i].num == num) return int(i);
    return -1;
}

void Basic::storeLine(int num, const std::string& src) {
    std::size_t b = 0;
    while (b < src.size() && src[b] == ' ') ++b;
    const std::string body = src.substr(b);

    std::size_t i = 0;
    while (i < lines_.size() && lines_[i].num < num) ++i;

    if (i < lines_.size() && lines_[i].num == num) {
        if (body.empty()) lines_.erase(lines_.begin() + i);
        else              lines_[i].src = body;
        return;
    }
    if (body.empty()) return;
    lines_.insert(lines_.begin() + i, Line{num, body});
}

void Basic::listProgram(int from, int to) {
    for (const auto& ln : lines_) {
        if (ln.num < from || ln.num > to) continue;
        const std::string num = formatNumber(ln.num);
        outText(num.c_str() + 1);   // strip the sign space
        outChar(' ');
        outText(ln.src);
        screen_.put(0x0D);
    }
}

// ---------------------------------------------------------------------------
// entry points
// ---------------------------------------------------------------------------

void Basic::execLine(const char* rawText) {
    const std::string text = upperNoQuotes(rawText);

    std::size_t p = 0;
    while (p < text.size() && text[p] == ' ') ++p;
    if (p >= text.size()) return;                      // empty line

    if (std::isdigit((unsigned char)text[p])) {        // program line
        int num = 0;
        while (p < text.size() && std::isdigit((unsigned char)text[p]))
            num = num * 10 + (text[p++] - '0');
        storeLine(num, text.substr(p));
        return;
    }

    directText_ = text;                                // direct command
    lineIdx_ = -1;
    loadLine();
    pos_ = 0;
    err_ = nullptr;
    mode_ = Mode::Running;
}

void Basic::pushKey(uint8_t k) {
    if (getBuf_.size() < 16) getBuf_ += char(k);
}

void Basic::breakRun() {
    if (mode_ == Mode::Running || mode_ == Mode::Input) breakReq_ = true;
}

// ---------------------------------------------------------------------------
// run loop
// ---------------------------------------------------------------------------

void Basic::loadLine() {
    if (lineIdx_ < 0) { src_ = directText_.c_str(); len_ = directText_.size(); }
    else              { src_ = lines_[lineIdx_].src.c_str(); len_ = lines_[lineIdx_].src.size(); }
}

void Basic::runProgram() {
    clearVars();
    if (lines_.empty()) { finishRun(); return; }
    lineIdx_ = 0;
    pos_ = 0;
    loadLine();
}

void Basic::finishRun() {
    if (screen_.cursorX() != 0) screen_.put(0x0D);
    outText("READY.");
    screen_.put(0x0D);
    mode_ = Mode::Idle;
}

void Basic::doBreak() {
    if (screen_.cursorX() != 0) screen_.put(0x0D);
    outText("BREAK");
    if (lineIdx_ >= 0 && lineIdx_ < int(lines_.size())) {
        outText(" IN ");
        outText(formatNumber(lines_[lineIdx_].num).c_str() + 1);
    }
    screen_.put(0x0D);
    contLine_ = lineIdx_; contPos_ = pos_; contOk_ = (lineIdx_ >= 0);
    mode_ = Mode::Idle;
}

void Basic::poll() {
    if (mode_ != Mode::Running) return;

    int budget = STEP_BUDGET;
    while (mode_ == Mode::Running && budget-- > 0) {
        if (breakReq_) { breakReq_ = false; doBreak(); return; }
        if (err_) { reportError(); finishRun(); return; }

        skipSpaces();
        if (pos_ >= len_) {
            if (lineIdx_ < 0) { finishRun(); return; }        // direct line done
            if (++lineIdx_ >= int(lines_.size())) { finishRun(); return; }
            pos_ = 0; loadLine();
            continue;
        }
        if (peek() == ':') { ++pos_; continue; }
        execStatement();
    }
}

// ---------------------------------------------------------------------------
// statement dispatch
// ---------------------------------------------------------------------------

void Basic::execStatement() {
    skipSpaces();
    if (atEnd() || peek() == ':') return;
    stmtStart_ = pos_;   // WAIT rewinds here to re-test without blocking

    if (peek() == '?') { ++pos_; stPrint(); return; }
    if (matchKw("PRINT#"))  { stPrintFile(); return; }
    if (matchKw("PRINT"))   { stPrint();  return; }
    if (matchKw("REM"))     { pos_ = len_; return; }
    if (matchKw("DATA"))    { pos_ = len_; return; }
    if (matchKw("IF"))      { stIf();     return; }
    if (matchKw("FOR"))     { stFor();    return; }
    if (matchKw("NEXT"))    { stNext();   return; }
    if (matchKw("GOTO"))    { stGoto();   return; }
    if (matchKw("GOSUB"))   { stGosub();  return; }
    if (matchKw("RETURN"))  { stReturn(); return; }
    if (matchKw("ON"))      { stOn();     return; }
    if (matchKw("INPUT#"))  { stInputFile(); return; }
    if (matchKw("INPUT"))   { stInput();  return; }
    if (matchKw("GET#"))    { stGetFile(); return; }
    if (matchKw("GET"))     { stGet();    return; }
    if (matchKw("READ"))    { stRead();   return; }
    if (matchKw("RESTORE")) { restoreData(); return; }
    if (matchKw("DIM"))     { stDim();    return; }
    if (matchKw("POKE"))    { stPoke();   return; }
    if (matchKw("WAIT"))    { stWait();   return; }
    if (matchKw("OPEN"))    { stOpen();   return; }
    if (matchKw("CLOSE"))   { stClose();  return; }
    if (matchKw("CMD"))     { stCmd();    return; }
    if (matchKw("VERIFY"))  { stVerify(); return; }
    if (matchKw("DEF"))     { stDef();    return; }
    if (matchKw("END"))     { finishRun(); return; }
    if (matchKw("STOP"))    { doBreak(); return; }
    if (matchKw("CONT"))    {
        if (contOk_) { lineIdx_ = contLine_; pos_ = contPos_; loadLine(); mode_ = Mode::Running; }
        else setError("CAN'T CONTINUE");
        return;
    }
    if (matchKw("RUN"))     { runProgram(); return; }
    if (matchKw("NEW"))     { lines_.clear(); clearVars(); return; }
    if (matchKw("CLR"))     { clearVars(); return; }
    if (matchKw("LIST")) {
        int from = 0, to = 2147483647;
        skipSpaces();
        if (!atEnd() && std::isdigit((unsigned char)peek())) {
            parseLineNumber(from); to = from;
            skipSpaces();
            if (peek() == '-') { ++pos_; skipSpaces();
                if (!atEnd() && std::isdigit((unsigned char)peek())) parseLineNumber(to);
                else to = 2147483647; }
        }
        listProgram(from, to);
        return;
    }
    if (matchKw("SAVE"))      { stSave();    return; }
    if (matchKw("LOAD"))      { stLoad();    return; }
    if (matchKw("SCRATCH"))   { stScratch(); return; }
    if (matchKw("DIRECTORY") || matchKw("DIR")) { directory(); return; }
    if (matchKw("LET")) { stAssign(); return; }
    stAssign();
}

// ---------------------------------------------------------------------------
// statements
// ---------------------------------------------------------------------------

void Basic::stPrint() {
    bool trailingSep = false;
    for (;;) {
        skipSpaces();
        if (atEnd() || peek() == ':') break;

        char c = peek();
        if (c == ';') { ++pos_; trailingSep = true; continue; }
        if (c == ',') {
            ++pos_; trailingSep = true;
            const int col = outColumn();
            const int target = ((col / 10) + 1) * 10;
            if (target >= Screen::COLS) outChar(0x0D);
            else while (outColumn() < target) outChar(' ');
            continue;
        }
        if (matchKw("TAB(")) {
            Value n = parseExpr(); if (err_) return;
            skipSpaces(); if (peek() == ')') ++pos_;
            const int t = int(n.num);
            while (outColumn() < t && outColumn() < Screen::COLS - 1) outChar(' ');
            trailingSep = false; continue;
        }
        if (matchKw("SPC(")) {
            Value n = parseExpr(); if (err_) return;
            skipSpaces(); if (peek() == ')') ++pos_;
            for (int i = 0; i < int(n.num); ++i) outChar(' ');
            trailingSep = false; continue;
        }

        Value v = parseExpr();
        if (err_) return;
        if (v.isStr) outText(v.str);
        else         outNumber(v.num);
        trailingSep = false;
    }
    if (!trailingSep) outChar(0x0D);
}

void Basic::stAssign() {
    skipSpaces();
    if (atEnd() || !std::isalpha((unsigned char)peek())) { setError("SYNTAX"); return; }
    assignTarget(Value{});   // assignTarget re-reads the lvalue and expects '=' expr
}

void Basic::assignTarget(const Value& preread) {
    // Two callers: stAssign parses "lvalue = expr"; READ passes the value in
    // `preread` and there is no '=' after the lvalue. We tell them apart by
    // whether an '=' follows the parsed lvalue.
    skipSpaces();
    uint8_t type = VT_NUM;
    std::string name = parseName(type);

    // TI$ sets the clock; TI and ST are read-only on the original.
    if (name == "TI" && type == VT_STR) {
        skipSpaces();
        if (peek() != '=') { setError("SYNTAX"); return; }
        ++pos_;
        Value v = parseExpr();
        if (err_) return;
        if (!v.isStr) { setError("TYPE MISMATCH"); return; }
        setTimeString(v.str);
        return;
    }
    if ((name == "TI" && type == VT_NUM) || (name == "ST" && type == VT_NUM)) {
        setError("SYNTAX"); return;
    }

    std::vector<int> idx;
    skipSpaces();
    bool isArray = false;
    if (peek() == '(') { idx = parseIndices(); if (err_) return; isArray = true; }

    // Assignment statement path: consume '=' and evaluate RHS.
    skipSpaces();
    Value v;
    bool fromExpr = false;
    if (peek() == '=') {
        ++pos_;
        v = parseExpr();
        if (err_) return;
        fromExpr = true;
    } else {
        v = preread;   // READ / INPUT# path supplies the value as text
    }

    const Value fitted = coerce(v, type, !fromExpr);
    if (err_) return;
    if (isArray) {
        Arr& a = ensureArr(name, type, int(idx.size()));
        const int f = flatIndex(a, idx);
        if (err_) return;
        if (type == VT_STR) a.s[f] = fitted.str; else a.num[f] = fitted.num;
    } else {
        setVar(name, type, fitted);
    }
}

void Basic::stIf() {
    Value cond = parseExpr();
    if (err_) return;
    const bool truth = !cond.isStr && cond.num != 0;

    skipSpaces();
    bool asGoto = false;
    if (matchKw("THEN"))      asGoto = false;
    else if (matchKw("GOTO")) asGoto = true;
    else { setError("SYNTAX"); return; }

    if (!truth) { pos_ = len_; return; }

    skipSpaces();
    if (asGoto || (!atEnd() && std::isdigit((unsigned char)peek()))) {
        int num = 0;
        if (!parseLineNumber(num)) { setError("SYNTAX"); return; }
        int idx = findLine(num);
        if (idx < 0) { setError("UNDEF'D STATEMENT"); return; }
        lineIdx_ = idx; pos_ = 0; loadLine();
    }
}

void Basic::stFor() {
    skipSpaces();
    uint8_t type = VT_NUM;
    std::string name = parseName(type);
    // Like the original: a loop counter must be plain floating point, so
    // FOR I%=... and FOR I$=... are both a syntax error.
    if (type != VT_NUM) { setError("SYNTAX"); return; }
    skipSpaces();
    if (peek() != '=') { setError("SYNTAX"); return; }
    ++pos_;
    Value start = parseExpr(); if (err_) return;
    setVar(name, VT_NUM, start);

    skipSpaces();
    if (!matchKw("TO")) { setError("SYNTAX"); return; }
    Value limit = parseExpr(); if (err_) return;

    double step = 1;
    skipSpaces();
    if (matchKw("STEP")) { Value s = parseExpr(); if (err_) return; step = s.num; }

    Var* v = findVar(name.size() ? name[0] : ' ', name.size() > 1 ? name[1] : ' ', VT_NUM);
    int varIdx = v ? int(v - &vars_[0]) : -1;
    forStack_.push_back(ForRec{varIdx, limit.num, step, lineIdx_, pos_});
}

void Basic::stNext() {
    skipSpaces();
    if (!atEnd() && std::isalpha((unsigned char)peek())) { uint8_t t; parseName(t); }

    if (forStack_.empty()) { setError("NEXT WITHOUT FOR"); return; }
    ForRec& f = forStack_.back();
    Var& var = vars_[f.varIdx];
    var.val.num += f.step;
    const bool cont = f.step >= 0 ? var.val.num <= f.limit : var.val.num >= f.limit;
    if (cont) { lineIdx_ = f.line; pos_ = f.pos; loadLine(); }
    else      { forStack_.pop_back(); }
}

void Basic::stGoto() {
    int num = 0;
    if (!parseLineNumber(num)) { setError("SYNTAX"); return; }
    int idx = findLine(num);
    if (idx < 0) { setError("UNDEF'D STATEMENT"); return; }
    lineIdx_ = idx; pos_ = 0; loadLine();
}

void Basic::stGosub() {
    int num = 0;
    if (!parseLineNumber(num)) { setError("SYNTAX"); return; }
    int idx = findLine(num);
    if (idx < 0) { setError("UNDEF'D STATEMENT"); return; }
    gosubStack_.push_back(SubRec{lineIdx_, pos_});
    lineIdx_ = idx; pos_ = 0; loadLine();
}

void Basic::stReturn() {
    if (gosubStack_.empty()) { setError("RETURN WITHOUT GOSUB"); return; }
    SubRec r = gosubStack_.back(); gosubStack_.pop_back();
    lineIdx_ = r.line; pos_ = r.pos; loadLine();
}

void Basic::stOn() {
    Value e = parseExpr(); if (err_) return;
    const int sel = int(e.num);
    if (sel < 0) { setError("ILLEGAL QUANTITY"); return; }
    skipSpaces();
    bool sub;
    if (matchKw("GOSUB")) sub = true;
    else if (matchKw("GOTO")) sub = false;
    else { setError("SYNTAX"); return; }

    std::vector<int> targets;
    for (;;) {
        int n; if (!parseLineNumber(n)) break;
        targets.push_back(n);
        skipSpaces();
        if (peek() == ',') { ++pos_; continue; }
        break;
    }
    if (sel >= 1 && sel <= int(targets.size())) {
        int idx = findLine(targets[sel - 1]);
        if (idx < 0) { setError("UNDEF'D STATEMENT"); return; }
        if (sub) gosubStack_.push_back(SubRec{lineIdx_, pos_});
        lineIdx_ = idx; pos_ = 0; loadLine();
    }
}

void Basic::stInput() {
    skipSpaces();
    if (peek() == '"') {                 // optional prompt
        ++pos_; std::string prompt;
        while (!atEnd() && peek() != '"') prompt += src_[pos_++];
        if (peek() == '"') ++pos_;
        outText(prompt);
        skipSpaces();
        if (peek() == ';') ++pos_;
    }
    outText("? ");

    inTargets_.clear();
    for (;;) {
        skipSpaces();
        uint8_t type = VT_NUM;
        std::string name = parseName(type);
        if (name.empty()) { setError("SYNTAX"); return; }
        inTargets_.push_back(InTarget{name[0], name.size() > 1 ? name[1] : ' ', type});
        skipSpaces();
        if (peek() == ',') { ++pos_; continue; }
        break;
    }
    mode_ = Mode::Input;   // suspend until provideInput()
}

void Basic::provideInput(const char* text) {
    // Split the submitted line on commas and assign to the pending targets.
    std::vector<std::string> fields;
    std::string cur;
    for (const char* p = text; *p; ++p) {
        if (*p == ',') { fields.push_back(cur); cur.clear(); }
        else cur += *p;
    }
    fields.push_back(cur);

    for (std::size_t i = 0; i < inTargets_.size(); ++i) {
        const InTarget& t = inTargets_[i];
        std::string f = i < fields.size() ? fields[i] : std::string();
        std::size_t a = f.find_first_not_of(' ');
        std::size_t b = f.find_last_not_of(' ');
        f = (a == std::string::npos) ? std::string() : f.substr(a, b - a + 1);
        std::string nm; nm += t.n0; if (t.n1 != ' ') nm += t.n1;
        setVar(nm, t.type, t.type == VT_STR ? Value::string(f)
                                            : Value::number(std::atof(f.c_str())));
    }
    inTargets_.clear();
    mode_ = Mode::Running;
}

void Basic::stGet() {
    skipSpaces();
    uint8_t type = VT_NUM;
    std::string name = parseName(type);
    if (name.empty()) { setError("SYNTAX"); return; }

    uint8_t k = 0;
    if (!getBuf_.empty()) { k = (uint8_t)getBuf_.front(); getBuf_.erase(getBuf_.begin()); }

    if (type == VT_STR) setVar(name, type, Value::string(k ? std::string(1, char(k)) : std::string()));
    else                setVar(name, type, Value::number((k >= '0' && k <= '9') ? k - '0' : 0));
}

void Basic::stDim() {
    for (;;) {
        skipSpaces();
        uint8_t type = VT_NUM;
        std::string name = parseName(type);
        if (name.empty()) { setError("SYNTAX"); return; }
        skipSpaces();
        if (peek() != '(') { setError("SYNTAX"); return; }
        std::vector<int> sizes = parseIndices();   // these are the max subscripts
        if (err_) return;
        char a = name[0], b = name.size() > 1 ? name[1] : ' ';
        if (findArr(a, b, type)) { setError("REDIM'D ARRAY"); return; }
        {
            Arr arr; arr.n0 = a; arr.n1 = b; arr.type = type;
            int total = 1;
            for (int s : sizes) { arr.dim.push_back(s + 1); total *= (s + 1); }
            if (type == VT_STR) arr.s.assign(total, std::string());
            else                arr.num.assign(total, 0.0);
            arrays_.push_back(std::move(arr));
        }
        skipSpaces();
        if (peek() == ',') { ++pos_; continue; }
        break;
    }
}

void Basic::stRead() {
    for (;;) {
        std::string item;
        if (!readData(item)) { setError("OUT OF DATA"); return; }
        assignTarget(Value::string(item));   // READ path: value supplied
        if (err_) return;
        skipSpaces();
        if (peek() == ',') { ++pos_; continue; }
        break;
    }
}

// A filename argument: SAVE "NAME"  /  LOAD "NAME". Upper-cased and clipped to
// 16 characters, the way a 1541 directory entry works.
void Basic::stPoke() {
    Value addr = parseExpr(); if (err_) return;
    skipSpaces();
    if (peek() != ',') { setError("SYNTAX"); return; }
    ++pos_;
    Value val = parseExpr(); if (err_) return;
    pokeMem(int(addr.num), uint8_t(int(val.num) & 0xFF));
}


// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// reserved variables: TI, TI$, FRE
// ---------------------------------------------------------------------------

// The jiffy clock ticks 60 times a second and wraps after 24 hours, like the
// original. It runs off the millis() the app feeds in, so nothing here needs a
// clock of its own.
long Basic::jiffies() const {
    const long running = long((uint64_t(nowMs_) * 60) / 1000);
    long t = (running + tiOffset_) % 5184000L;      // 24 h in jiffies
    if (t < 0) t += 5184000L;
    return t;
}

std::string Basic::timeString() const {
    const long secs = jiffies() / 60;
    char buf[8];
    std::snprintf(buf, sizeof buf, "%02ld%02ld%02ld",
                  (secs / 3600) % 24, (secs / 60) % 60, secs % 60);
    return buf;
}

void Basic::setTimeString(const std::string& hhmmss) {
    if (hhmmss.size() != 6) { setError("ILLEGAL QUANTITY"); return; }
    for (char c : hhmmss) if (!std::isdigit((unsigned char)c)) { setError("ILLEGAL QUANTITY"); return; }
    const long h = std::atol(hhmmss.substr(0, 2).c_str());
    const long m = std::atol(hhmmss.substr(2, 2).c_str());
    const long sec = std::atol(hhmmss.substr(4, 2).c_str());
    const long want = ((h * 3600) + (m * 60) + sec) * 60;
    tiOffset_ = want - long((uint64_t(nowMs_) * 60) / 1000);
}

// What is left of the 38911 bytes the boot screen advertises. The original
// returns this through a signed 16-bit register, so anything above 32767 comes
// back negative and you add 65536 — a quirk every C64 programmer knows.
long Basic::freeBytes() const {
    long used = 0;
    for (const auto& ln : lines_) used += long(ln.src.size()) + 5;
    for (const auto& v : vars_) used += 7 + long(v.val.str.size());
    for (const auto& a : arrays_) {
        long cells = 1;
        for (int d : a.dim) cells *= d;
        used += 5 + cells * (a.type == VT_STR ? 3 : 5);
        for (const auto& str : a.s) used += long(str.size());
    }
    long free = 38911 - used;
    if (free < 0) free = 0;
    if (free > 32767) free -= 65536;      // the signed-16-bit quirk
    return free;
}

// WAIT addr, mask [,xor] — spin until (PEEK(addr) XOR xor) AND mask is set.
// Instead of blocking, it rewinds to the start of the statement so the stepped
// run loop keeps its budget and RUN/STOP still works.
void Basic::stWait() {
    Value addr = parseExpr(); if (err_) return;
    skipSpaces();
    if (peek() != ',') { setError("SYNTAX"); return; }
    ++pos_;
    Value mask = parseExpr(); if (err_) return;
    int flip = 0;
    skipSpaces();
    if (peek() == ',') { ++pos_; Value x = parseExpr(); if (err_) return; flip = int(x.num); }

    const int v = (peekMem(int(addr.num)) ^ (flip & 0xFF)) & (int(mask.num) & 0xFF);
    if (v == 0) pos_ = stmtStart_;   // not yet: run the whole statement again
}

// ---------------------------------------------------------------------------
// memory map — the addresses every C64 listing pokes at
// ---------------------------------------------------------------------------

namespace {
constexpr int SCREEN_RAM = 1024;    // $0400, 40x25 screen codes
constexpr int COLOR_RAM  = 55296;   // $D800, one colour nibble per cell
constexpr int VIC_BORDER = 53280;   // $D020
constexpr int VIC_BG     = 53281;   // $D021
constexpr int SID_BASE   = 54272;   // $D400, 29 registers
}

void Basic::pokeMem(int addr, uint8_t value) {
    addr &= 0xFFFF;
    if (addr >= SCREEN_RAM && addr < SCREEN_RAM + Screen::CELLS) {
        screen_.pokeScreen(addr - SCREEN_RAM, value);
    } else if (addr >= COLOR_RAM && addr < COLOR_RAM + Screen::CELLS) {
        screen_.pokeColor(addr - COLOR_RAM, value);
    } else if (addr == VIC_BORDER) {
        screen_.setBorder(value);
    } else if (addr == VIC_BG) {
        screen_.setBackground(value);
    } else if (addr >= SID_BASE && addr < SID_BASE + Sid::NUM_REGS) {
        if (sid_) sid_->write(addr - SID_BASE, value);
    } else if (ram_) {
        ram_[addr] = value;
    }
}

uint8_t Basic::peekMem(int addr) const {
    addr &= 0xFFFF;
    if (addr >= SCREEN_RAM && addr < SCREEN_RAM + Screen::CELLS)
        return screen_.peekScreen(addr - SCREEN_RAM);
    if (addr >= COLOR_RAM && addr < COLOR_RAM + Screen::CELLS)
        return screen_.peekColor(addr - COLOR_RAM);
    // The real VIC returns the unused top bits as 1s; we hand back the plain
    // 0..15 so POKE 53280,PEEK(53280)+1 cycles colours the obvious way.
    if (addr == VIC_BORDER) return screen_.border();
    if (addr == VIC_BG)     return screen_.background();
    // Only the two oscillator taps read back; the rest of the SID is
    // write-only and answers 0, exactly like the chip.
    if (addr >= SID_BASE && addr < SID_BASE + Sid::NUM_REGS)
        return sid_ ? sid_->read(addr - SID_BASE) : 0;
    return ram_ ? ram_[addr] : 0;
}

void Basic::stDef() {
    skipSpaces();
    if (!matchKw("FN")) { setError("SYNTAX"); return; }
    skipSpaces();
    uint8_t t = VT_NUM;
    std::string name = parseName(t);
    skipSpaces();
    if (peek() != '(') { setError("SYNTAX"); return; }
    ++pos_;
    uint8_t pt = VT_NUM;
    std::string param = parseName(pt);
    skipSpaces();
    if (peek() != ')') { setError("SYNTAX"); return; }
    ++pos_;
    skipSpaces();
    if (peek() != '=') { setError("SYNTAX"); return; }
    ++pos_;
    std::string expr;
    while (!atEnd() && peek() != ':') expr += src_[pos_++];

    char a = name[0], b = name.size() > 1 ? name[1] : ' ';
    for (auto& f : fns_) if (f.n0 == a && f.n1 == b) { f.param = param; f.expr = expr; return; }
    fns_.push_back(Fn{a, b, param, expr});
}

// ---------------------------------------------------------------------------
// DATA / READ
// ---------------------------------------------------------------------------

bool Basic::readData(std::string& out) {
    for (;;) {
        if (dataLine_ >= int(lines_.size())) return false;
        const std::string& s = lines_[dataLine_].src;
        if (!dataInStmt_) {
            std::size_t f = s.find("DATA", dataPos_);
            if (f == std::string::npos) { ++dataLine_; dataPos_ = 0; continue; }
            dataPos_ = f + 4; dataInStmt_ = true;
        }
        while (dataPos_ < s.size() && s[dataPos_] == ' ') ++dataPos_;
        if (dataPos_ >= s.size()) { dataInStmt_ = false; ++dataLine_; dataPos_ = 0; continue; }

        std::string item;
        if (s[dataPos_] == '"') { ++dataPos_;
            while (dataPos_ < s.size() && s[dataPos_] != '"') item += s[dataPos_++];
            if (dataPos_ < s.size()) ++dataPos_;
        } else {
            while (dataPos_ < s.size() && s[dataPos_] != ',' && s[dataPos_] != ':') item += s[dataPos_++];
            while (!item.empty() && item.back() == ' ') item.pop_back();
        }
        if (dataPos_ < s.size() && s[dataPos_] == ',') ++dataPos_;
        else { dataInStmt_ = false; ++dataLine_; dataPos_ = 0; }
        out = item;
        return true;
    }
}

// ---------------------------------------------------------------------------
// output
// ---------------------------------------------------------------------------

void Basic::outChar(char c) {
    if (printTo_ >= 0) {
        if (OpenFile* f = findFile(printTo_)) { f->buf += c; return; }
    }
    screen_.put(uint8_t(c));
}

// Column of whatever output is currently active, so PRINT's comma zones work
// the same whether the text lands on the screen or in a file.
int Basic::outColumn() {
    if (printTo_ >= 0) {
        if (OpenFile* f = findFile(printTo_)) {
            const std::size_t nl = f->buf.rfind('\r');
            return int(nl == std::string::npos ? f->buf.size() : f->buf.size() - nl - 1);
        }
    }
    return screen_.cursorX();
}
void Basic::outText(const char* s) { while (*s) outChar(*s++); }

std::string Basic::formatNumber(double v) {
    char buf[40];
    if (v == std::floor(v) && std::fabs(v) < 1e15) std::snprintf(buf, sizeof buf, "%lld", (long long)v);
    else std::snprintf(buf, sizeof buf, "%.9g", v);
    std::string s = (v >= 0) ? std::string(" ") + buf : std::string(buf);
    // C64 drops the leading zero of fractions: 0.25 -> .25, -0.25 -> -.25
    std::size_t p = (s[0] == ' ' || s[0] == '-') ? 1 : 0;
    if (s.size() > p + 1 && s[p] == '0' && s[p + 1] == '.') s.erase(p, 1);
    return s;
}
void Basic::outNumber(double v) { outText(formatNumber(v)); outChar(' '); }

// ---------------------------------------------------------------------------
// errors
// ---------------------------------------------------------------------------

void Basic::setError(const char* msg) { if (!err_) err_ = msg; }

void Basic::reportError() {
    if (screen_.cursorX() != 0) screen_.put(0x0D);
    outChar('?');
    outText(err_);
    outText(" ERROR");
    if (lineIdx_ >= 0 && lineIdx_ < int(lines_.size())) {
        outText(" IN ");
        outText(formatNumber(lines_[lineIdx_].num).c_str() + 1);
    }
    screen_.put(0x0D);
    err_ = nullptr;
}

} // namespace apps::ghost
