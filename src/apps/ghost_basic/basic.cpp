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

void Basic::doBreak(bool /*stopKeyword*/) {
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
        if (breakReq_) { breakReq_ = false; doBreak(false); return; }
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

    if (peek() == '?') { ++pos_; stPrint(); return; }
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
    if (matchKw("INPUT"))   { stInput();  return; }
    if (matchKw("GET"))     { stGet();    return; }
    if (matchKw("READ"))    { stRead();   return; }
    if (matchKw("RESTORE")) { restoreData(); return; }
    if (matchKw("DIM"))     { stDim();    return; }
    if (matchKw("POKE"))    { stPoke();   return; }
    if (matchKw("DEF"))     { stDef();    return; }
    if (matchKw("END"))     { finishRun(); return; }
    if (matchKw("STOP"))    { doBreak(true); return; }
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
    if (matchKw("LET")) { stAssign(true); return; }
    stAssign(false);
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
            int col = screen_.cursorX();
            int target = ((col / 10) + 1) * 10;
            if (target >= Screen::COLS) screen_.put(0x0D);
            else while (screen_.cursorX() < target) screen_.put(' ');
            continue;
        }
        if (matchKw("TAB(")) {
            Value n = parseExpr(); if (err_) return;
            skipSpaces(); if (peek() == ')') ++pos_;
            int t = int(n.num);
            while (screen_.cursorX() < t && screen_.cursorX() < Screen::COLS - 1) screen_.put(' ');
            trailingSep = false; continue;
        }
        if (matchKw("SPC(")) {
            Value n = parseExpr(); if (err_) return;
            skipSpaces(); if (peek() == ')') ++pos_;
            for (int i = 0; i < int(n.num); ++i) screen_.put(' ');
            trailingSep = false; continue;
        }

        Value v = parseExpr();
        if (err_) return;
        if (v.isStr) outText(v.str);
        else         outNumber(v.num);
        trailingSep = false;
    }
    if (!trailingSep) screen_.put(0x0D);
}

void Basic::stAssign(bool) {
    skipSpaces();
    if (atEnd() || !std::isalpha((unsigned char)peek())) { setError("SYNTAX"); return; }
    assignTarget(Value{});   // assignTarget re-reads the lvalue and expects '=' expr
}

void Basic::assignTarget(const Value& preread) {
    // Two callers: stAssign parses "lvalue = expr"; READ passes the value in
    // `preread` and there is no '=' after the lvalue. We tell them apart by
    // whether an '=' follows the parsed lvalue.
    skipSpaces();
    bool isStr = false;
    std::string name = parseName(isStr);
    std::vector<int> idx;
    skipSpaces();
    bool isArray = false;
    if (peek() == '(') { idx = parseIndices(); if (err_) return; isArray = true; }

    // Assignment statement path: consume '=' and evaluate RHS.
    skipSpaces();
    Value v;
    if (peek() == '=') {
        ++pos_;
        v = parseExpr();
        if (err_) return;
    } else {
        v = preread;   // READ path supplies the value
    }

    if (isStr) {
        std::string s = v.isStr ? v.str : formatNumber(v.num);
        if (isArray) { Arr& a = ensureArr(name, true, int(idx.size()));
            int f = flatIndex(a, idx); if (err_) return; a.s[f] = s; }
        else setVar(name, true, Value::string(s));
    } else {
        double d = v.isStr ? std::atof(v.str.c_str()) : v.num;
        if (isArray) { Arr& a = ensureArr(name, false, int(idx.size()));
            int f = flatIndex(a, idx); if (err_) return; a.num[f] = d; }
        else setVar(name, false, Value::number(d));
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
    bool isStr = false;
    std::string name = parseName(isStr);
    if (isStr) { setError("SYNTAX"); return; }
    skipSpaces();
    if (peek() != '=') { setError("SYNTAX"); return; }
    ++pos_;
    Value start = parseExpr(); if (err_) return;
    setVar(name, false, start);

    skipSpaces();
    if (!matchKw("TO")) { setError("SYNTAX"); return; }
    Value limit = parseExpr(); if (err_) return;

    double step = 1;
    skipSpaces();
    if (matchKw("STEP")) { Value s = parseExpr(); if (err_) return; step = s.num; }

    Var* v = findVar(name.size() ? name[0] : ' ', name.size() > 1 ? name[1] : ' ', false);
    int varIdx = v ? int(v - &vars_[0]) : -1;
    forStack_.push_back(ForRec{varIdx, limit.num, step, lineIdx_, pos_});
}

void Basic::stNext() {
    skipSpaces();
    if (!atEnd() && std::isalpha((unsigned char)peek())) { bool s; parseName(s); }

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
    int sel = int(e.num);
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
        bool isStr = false;
        std::string name = parseName(isStr);
        if (name.empty()) { setError("SYNTAX"); return; }
        inTargets_.push_back(InTarget{name[0], name.size() > 1 ? name[1] : ' ', isStr});
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
        if (t.str) setVar(nm, true, Value::string(f));
        else       setVar(nm, false, Value::number(std::atof(f.c_str())));
    }
    inTargets_.clear();
    mode_ = Mode::Running;
}

void Basic::stGet() {
    skipSpaces();
    bool isStr = false;
    std::string name = parseName(isStr);
    if (name.empty()) { setError("SYNTAX"); return; }

    uint8_t k = 0;
    if (!getBuf_.empty()) { k = (uint8_t)getBuf_.front(); getBuf_.erase(getBuf_.begin()); }

    if (isStr) setVar(name, true, Value::string(k ? std::string(1, char(k)) : std::string()));
    else       setVar(name, false, Value::number((k >= '0' && k <= '9') ? k - '0' : 0));
}

void Basic::stDim() {
    for (;;) {
        skipSpaces();
        bool isStr = false;
        std::string name = parseName(isStr);
        if (name.empty()) { setError("SYNTAX"); return; }
        skipSpaces();
        if (peek() != '(') { setError("SYNTAX"); return; }
        std::vector<int> sizes = parseIndices();   // these are the max subscripts
        if (err_) return;
        char a = name[0], b = name.size() > 1 ? name[1] : ' ';
        Arr* existing = findArr(a, b, isStr);
        if (!existing) {
            Arr arr; arr.n0 = a; arr.n1 = b; arr.str = isStr;
            int total = 1;
            for (int s : sizes) { arr.dim.push_back(s + 1); total *= (s + 1); }
            if (isStr) arr.s.assign(total, std::string());
            else       arr.num.assign(total, 0.0);
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
bool Basic::parseFileName(std::string& out) {
    Value v = parseExpr();
    if (err_) return false;
    if (!v.isStr) { setError("TYPE MISMATCH"); return false; }
    out.clear();
    for (char c : v.str) {
        if (out.size() >= 16) break;
        out += (c >= 'a' && c <= 'z') ? char(c - 32) : c;
    }
    if (out.empty()) { setError("MISSING FILE NAME"); return false; }
    return true;
}

void Basic::stSave() {
    std::string name;
    if (!parseFileName(name)) return;
    if (!files_) { setError("DEVICE NOT PRESENT"); return; }

    std::string text;
    for (const auto& ln : lines_) {
        const std::string num = formatNumber(ln.num);
        text += num.c_str() + 1;   // drop the sign space
        text += ' ';
        text += ln.src;
        text += '\n';
    }
    if (!files_->save(name.c_str(), text)) { setError("DEVICE NOT PRESENT"); return; }

    outText("SAVING ");
    outText(name);
    screen_.put(0x0D);
}

void Basic::stLoad() {
    std::string name;
    if (!parseFileName(name)) return;
    if (!files_) { setError("DEVICE NOT PRESENT"); return; }

    if (name == "$") { directory(); return; }   // LOAD "$" shows the directory

    std::string text;
    if (!files_->load(name.c_str(), text)) { setError("FILE NOT FOUND"); return; }

    outText("LOADING ");
    outText(name);
    screen_.put(0x0D);

    lines_.clear();
    clearVars();
    std::size_t i = 0;
    while (i < text.size()) {
        std::size_t e = text.find('\n', i);
        if (e == std::string::npos) e = text.size();
        std::string row = text.substr(i, e - i);
        i = e + 1;
        std::size_t p = 0;
        while (p < row.size() && row[p] == ' ') ++p;
        if (p >= row.size() || !std::isdigit((unsigned char)row[p])) continue;
        int num = 0;
        while (p < row.size() && std::isdigit((unsigned char)row[p]))
            num = num * 10 + (row[p++] - '0');
        storeLine(num, row.substr(p));
    }
}

void Basic::stScratch() {
    std::string name;
    if (!parseFileName(name)) return;
    if (!files_) { setError("DEVICE NOT PRESENT"); return; }
    if (!files_->remove(name.c_str())) { setError("FILE NOT FOUND"); return; }
    outText("SCRATCHED ");
    outText(name);
    screen_.put(0x0D);
}

// A 1541-style listing: header line, one entry per program with its size in
// 254-byte blocks, then the free space.
void Basic::directory() {
    if (!files_) { setError("DEVICE NOT PRESENT"); return; }
    std::vector<Files::Entry> entries;
    if (!files_->list(entries)) { setError("DEVICE NOT PRESENT"); return; }

    {
        char hdr[48];
        std::snprintf(hdr, sizeof hdr, "0 \"GHOST BASIC\"     %s 2A", files_->volumeId());
        outText(hdr);
    }
    screen_.put(0x0D);
    for (const auto& e : entries) {
        const uint32_t blocks = (e.size + 253) / 254;
        char buf[48];
        std::snprintf(buf, sizeof buf, "%-4u \"%s\"", unsigned(blocks), e.name.c_str());
        outText(buf);
        int pad = 28 - int(std::strlen(buf));
        for (int i = 0; i < pad; ++i) outChar(' ');
        outText("PRG");
        screen_.put(0x0D);
    }
    char buf[40];
    std::snprintf(buf, sizeof buf, "%u BLOCKS FREE.", unsigned(files_->freeBytes() / 254));
    outText(buf);
    screen_.put(0x0D);
}

void Basic::stPoke() {
    Value addr = parseExpr(); if (err_) return;
    skipSpaces();
    if (peek() != ',') { setError("SYNTAX"); return; }
    ++pos_;
    Value val = parseExpr(); if (err_) return;
    if (ram_) ram_[int(addr.num) & 0xFFFF] = uint8_t(int(val.num) & 0xFF);
}

void Basic::stDef() {
    skipSpaces();
    if (!matchKw("FN")) { setError("SYNTAX"); return; }
    skipSpaces();
    bool s = false;
    std::string name = parseName(s);
    skipSpaces();
    if (peek() != '(') { setError("SYNTAX"); return; }
    ++pos_;
    bool ps = false;
    std::string param = parseName(ps);
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

void Basic::outChar(char c) { screen_.put(uint8_t(c)); }
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
