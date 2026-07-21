// Ghost BASIC — the language half of the interpreter: the lexer, the
// recursive-descent expression parser, the built-in functions, and variable and
// array storage. The machine half (run loop, statements, files) is in
// basic.cpp. Both are the same Basic class; the split is by concern, so each
// file stays readable on its own.
#include "basic.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>

namespace apps::ghost {

// ---------------------------------------------------------------------------
// expressions
// ---------------------------------------------------------------------------

Basic::Value Basic::parseExpr() { return parseOr(); }

Basic::Value Basic::parseOr() {
    Value a = parseAnd();
    for (;;) { skipSpaces();
        if (matchKw("OR")) { Value b = parseAnd(); if (err_) return a;
            a = Value::number(double(int32_t(a.num) | int32_t(b.num))); }
        else break; }
    return a;
}
Basic::Value Basic::parseAnd() {
    Value a = parseNot();
    for (;;) { skipSpaces();
        if (matchKw("AND")) { Value b = parseNot(); if (err_) return a;
            a = Value::number(double(int32_t(a.num) & int32_t(b.num))); }
        else break; }
    return a;
}
Basic::Value Basic::parseNot() {
    skipSpaces();
    if (matchKw("NOT")) { Value a = parseNot(); return Value::number(double(~int32_t(a.num))); }
    return parseCmp();
}
Basic::Value Basic::parseCmp() {
    Value a = parseAdd();
    skipSpaces();
    int op = 0;
    if (peek() == '=') { op = 1; ++pos_; }
    else if (peek() == '<') { ++pos_;
        if (peek() == '>') { op = 2; ++pos_; }
        else if (peek() == '=') { op = 5; ++pos_; } else op = 3; }
    else if (peek() == '>') { ++pos_;
        if (peek() == '=') { op = 6; ++pos_; } else op = 4; }
    if (!op) return a;
    Value b = parseAdd(); if (err_) return a;
    bool r;
    if (a.isStr && b.isStr) { int c = a.str.compare(b.str);
        switch (op) { case 1: r = c == 0; break; case 2: r = c != 0; break;
            case 3: r = c < 0; break; case 4: r = c > 0; break;
            case 5: r = c <= 0; break; default: r = c >= 0; } }
    else { double x = a.num, y = b.num;
        switch (op) { case 1: r = x == y; break; case 2: r = x != y; break;
            case 3: r = x < y; break; case 4: r = x > y; break;
            case 5: r = x <= y; break; default: r = x >= y; } }
    return Value::number(r ? -1.0 : 0.0);
}
Basic::Value Basic::parseAdd() {
    Value a = parseMul();
    for (;;) { skipSpaces(); char c = peek();
        if (c == '+') { ++pos_; Value b = parseMul(); if (err_) return a;
            if (a.isStr || b.isStr) {
                if (a.isStr != b.isStr) { setError("TYPE MISMATCH"); return a; }
                a = Value::string(a.str + b.str);
            } else a = Value::number(a.num + b.num); }
        else if (c == '-') { ++pos_; Value b = parseMul(); if (err_) return a;
            a = Value::number(a.num - b.num); }
        else break; }
    return a;
}
Basic::Value Basic::parseMul() {
    Value a = parseUnary();
    for (;;) { skipSpaces(); char c = peek();
        if (c == '*') { ++pos_; Value b = parseUnary(); if (err_) return a;
            a = Value::number(a.num * b.num); }
        else if (c == '/') { ++pos_; Value b = parseUnary(); if (err_) return a;
            if (b.num == 0) { setError("DIVISION BY ZERO"); return a; }
            a = Value::number(a.num / b.num); }
        else break; }
    return a;
}
// Precedence (C64): ^ binds tighter than unary minus, so -2^2 = -(2^2) = -4.
Basic::Value Basic::parseUnary() {
    skipSpaces();
    if (peek() == '-') { ++pos_; Value a = parseUnary(); return Value::number(-a.num); }
    if (peek() == '+') { ++pos_; return parseUnary(); }
    return parsePow();
}
Basic::Value Basic::parsePow() {
    Value a = parsePrimary();
    skipSpaces();
    if (peek() == '^') { ++pos_; Value b = parseUnary(); if (err_) return a;  // right operand may be negative / right-assoc
        return Value::number(std::pow(a.num, b.num)); }
    return a;
}
Basic::Value Basic::parsePrimary() {
    skipSpaces();
    char c = peek();
    if (c == '(') { ++pos_; Value v = parseExpr(); skipSpaces();
        if (peek() == ')') ++pos_; else setError("SYNTAX"); return v; }
    if (c == '"') { ++pos_; std::string s;
        while (!atEnd() && peek() != '"') s += src_[pos_++];
        if (peek() == '"') ++pos_; return Value::string(s); }
    if (std::isdigit((unsigned char)c) || c == '.') {
        const char* start = src_ + pos_; char* end = nullptr;
        double d = std::strtod(start, &end);
        pos_ += std::size_t(end - start); return Value::number(d); }
    if (matchKw("FN")) { skipSpaces(); uint8_t t; std::string nm = parseName(t); return callFn(nm); }
    if (std::isalpha((unsigned char)c)) {
        uint8_t type = VT_NUM;
        std::string name = parseName(type);

        // Reserved variables, read-only: the jiffy clock and the I/O status.
        if (name == "TI" && type == VT_NUM) return Value::number(double(jiffies()));
        if (name == "TI" && type == VT_STR) return Value::string(timeString());
        if (name == "ST" && type == VT_NUM) return Value::number(st_);  // 64 == end of file

        const std::string fname = type == VT_STR ? name + "$" : name;  // functions keep the $
        skipSpaces();
        if (peek() == '(') {
            if (isFunction(fname)) return callFunction(fname);
            return getArr(name, type);
        }
        return getVar(name, type);
    }
    setError("SYNTAX");
    return Value::number(0);
}

bool Basic::isFunction(const std::string& n) {
    static const char* F[] = {"ABS","INT","SGN","SQR","SIN","COS","TAN","ATN","EXP",
        "LOG","RND","LEN","ASC","VAL","PEEK","FRE","POS","CHR$","STR$","LEFT$","RIGHT$","MID$"};
    for (const char* f : F) if (n == f) return true;
    return false;
}

Basic::Value Basic::callFunction(const std::string& name) {
    ++pos_;   // '('
    Value a = parseExpr();
    auto nextArg = [&]() -> Value { skipSpaces();
        if (peek() == ',') { ++pos_; return parseExpr(); }
        setError("SYNTAX"); return Value::number(0); };

    Value r = Value::number(0);
    if      (name == "ABS") r = Value::number(std::fabs(a.num));
    else if (name == "INT") r = Value::number(std::floor(a.num));
    else if (name == "SGN") r = Value::number(a.num > 0 ? 1 : a.num < 0 ? -1 : 0);
    else if (name == "SQR") { if (a.num < 0) setError("ILLEGAL QUANTITY");
                              else r = Value::number(std::sqrt(a.num)); }
    else if (name == "SIN") r = Value::number(std::sin(a.num));
    else if (name == "COS") r = Value::number(std::cos(a.num));
    else if (name == "TAN") r = Value::number(std::tan(a.num));
    else if (name == "ATN") r = Value::number(std::atan(a.num));
    else if (name == "EXP") { if (a.num > 88) setError("OVERFLOW");
                              else r = Value::number(std::exp(a.num)); }
    else if (name == "LOG") { if (a.num <= 0) setError("ILLEGAL QUANTITY");
                              else r = Value::number(std::log(a.num)); }
    else if (name == "PEEK") r = Value::number(peekMem(int(a.num)));
    else if (name == "FRE")  r = Value::number(double(freeBytes()));
    else if (name == "POS")  r = Value::number(screen_.cursorX());
    else if (name == "LEN") r = Value::number(double(a.str.size()));
    else if (name == "ASC") { if (a.str.empty()) setError("ILLEGAL QUANTITY");
                              else r = Value::number((unsigned char)a.str[0]); }
    else if (name == "VAL") r = Value::number(std::atof(a.str.c_str()));
    else if (name == "CHR$") { const int c = int(a.num);
                               if (c < 0 || c > 255) setError("ILLEGAL QUANTITY");
                               else r = Value::string(std::string(1, char(c))); }
    else if (name == "STR$") r = Value::string(formatNumber(a.num));
    else if (name == "RND") { rngState_ = rngState_ * 1103515245u + 12345u;
        r = Value::number(((rngState_ >> 16) & 0x7FFF) / 32768.0); }
    else if (name == "LEFT$") { Value n = nextArg(); if (err_) return r;
        if (n.num < 0 || n.num > 255) { setError("ILLEGAL QUANTITY"); return r; }
        r = Value::string(a.str.substr(0, std::min(std::size_t(n.num), a.str.size()))); }
    else if (name == "RIGHT$") { Value n = nextArg(); if (err_) return r;
        if (n.num < 0 || n.num > 255) { setError("ILLEGAL QUANTITY"); return r; }
        const int k = int(n.num), st = std::max(0, int(a.str.size()) - k);
        r = Value::string(a.str.substr(std::size_t(st))); }
    else if (name == "MID$") { Value s = nextArg(); if (err_) return r;
        // The original counts from 1; a start of 0 is an error, not a clamp.
        if (s.num < 1 || s.num > 255) { setError("ILLEGAL QUANTITY"); return r; }
        const int start = int(s.num) - 1; int cnt = int(a.str.size());
        skipSpaces(); if (peek() == ',') { Value l = nextArg(); if (err_) return r; cnt = int(l.num); }
        r = (start >= int(a.str.size())) ? Value::string("")
            : Value::string(a.str.substr(std::size_t(start), std::size_t(std::max(0, cnt)))); }

    skipSpaces();
    if (peek() == ')') ++pos_; else if (!err_) setError("SYNTAX");
    return r;
}

Basic::Value Basic::callFn(const std::string& name) {
    skipSpaces();
    if (peek() != '(') { setError("SYNTAX"); return Value::number(0); }
    ++pos_;
    Value arg = parseExpr();
    skipSpaces();
    if (peek() == ')') ++pos_; else { setError("SYNTAX"); return Value::number(0); }

    char a = name.size() ? name[0] : ' ', b = name.size() > 1 ? name[1] : ' ';
    Fn* fn = nullptr;
    for (auto& f : fns_) if (f.n0 == a && f.n1 == b) { fn = &f; break; }
    if (!fn) { setError("UNDEF'D FUNCTION"); return Value::number(0); }

    // Bind the parameter, evaluate the stored expression with a sub-lexer.
    std::string pname = fn->param;
    Value saved = getVar(pname, VT_NUM);
    setVar(pname, VT_NUM, arg);

    const char* prevSrc = src_; std::size_t prevPos = pos_, prevLen = len_;
    std::string body = fn->expr;
    src_ = body.c_str(); len_ = body.size(); pos_ = 0;
    Value res = parseExpr();
    src_ = prevSrc; len_ = prevLen; pos_ = prevPos;

    setVar(pname, VT_NUM, saved);
    return res;
}

// ---------------------------------------------------------------------------
// variables & arrays
// ---------------------------------------------------------------------------

Basic::Value Basic::coerce(const Value& v, uint8_t type, bool lenient) {
    // An assignment may not cross the number/string divide — the original says
    // ?TYPE MISMATCH. Only data arriving as text (READ, INPUT#) is converted.
    if (!lenient && (type == VT_STR) != v.isStr) { setError("TYPE MISMATCH"); return v; }
    if (type == VT_STR) return v.isStr ? v : Value::string(formatNumber(v.num));
    const double d = v.isStr ? std::atof(v.str.c_str()) : v.num;
    if (type != VT_INT) return Value::number(d);
    const double t = d < 0 ? std::ceil(d) : std::floor(d);   // truncate toward zero
    if (t < -32768.0 || t > 32767.0) { setError("ILLEGAL QUANTITY"); return Value::number(0); }
    return Value::number(t);
}

Basic::Value Basic::getVar(const std::string& name, uint8_t type) {
    char a = name.size() ? name[0] : ' ', b = name.size() > 1 ? name[1] : ' ';
    Var* v = findVar(a, b, type);
    if (v) return v->val;
    return type == VT_STR ? Value::string("") : Value::number(0);
}
Basic::Var* Basic::findVar(char a, char b, uint8_t type) {
    for (auto& v : vars_) if (v.n0 == a && v.n1 == b && v.type == type) return &v;
    return nullptr;
}
void Basic::setVar(const std::string& name, uint8_t type, const Value& val) {
    char a = name.size() ? name[0] : ' ', b = name.size() > 1 ? name[1] : ' ';
    const Value fitted = coerce(val, type, true);
    Var* v = findVar(a, b, type);
    if (v) { v->val = fitted; return; }
    vars_.push_back(Var{a, b, type, fitted});
}

Basic::Arr* Basic::findArr(char a, char b, uint8_t type) {
    for (auto& x : arrays_) if (x.n0 == a && x.n1 == b && x.type == type) return &x;
    return nullptr;
}
Basic::Arr& Basic::ensureArr(const std::string& name, uint8_t type, int ndims) {
    char a = name.size() ? name[0] : ' ', b = name.size() > 1 ? name[1] : ' ';
    Arr* e = findArr(a, b, type);
    if (e) return *e;
    Arr arr; arr.n0 = a; arr.n1 = b; arr.type = type;
    int total = 1;
    for (int i = 0; i < ndims; ++i) { arr.dim.push_back(11); total *= 11; }  // auto-dim 0..10
    if (type == VT_STR) arr.s.assign(total, std::string()); else arr.num.assign(total, 0.0);
    arrays_.push_back(std::move(arr));
    return arrays_.back();
}
std::vector<int> Basic::parseIndices() {
    std::vector<int> idx;
    ++pos_;   // '('
    for (;;) { Value v = parseExpr(); if (err_) return idx;
        idx.push_back(int(v.num)); skipSpaces();
        if (peek() == ',') { ++pos_; continue; }
        break; }
    skipSpaces();
    if (peek() == ')') ++pos_; else setError("SYNTAX");
    return idx;
}
int Basic::flatIndex(const Arr& a, const std::vector<int>& idx) {
    if (idx.size() != a.dim.size()) { setError("BAD SUBSCRIPT"); return 0; }
    int flat = 0;
    for (std::size_t i = 0; i < idx.size(); ++i) {
        if (idx[i] < 0 || idx[i] >= a.dim[i]) { setError("BAD SUBSCRIPT"); return 0; }
        flat = flat * a.dim[i] + idx[i];
    }
    return flat;
}
Basic::Value Basic::getArr(const std::string& name, uint8_t type) {
    std::vector<int> idx = parseIndices();
    if (err_) return Value::number(0);
    Arr& a = ensureArr(name, type, int(idx.size()));
    int f = flatIndex(a, idx);
    if (err_) return Value::number(0);
    return type == VT_STR ? Value::string(a.s[f]) : Value::number(a.num[f]);
}


// ---------------------------------------------------------------------------
// lexer
// ---------------------------------------------------------------------------

void Basic::skipSpaces() { while (pos_ < len_ && src_[pos_] == ' ') ++pos_; }
bool Basic::atEnd() const { return pos_ >= len_; }
char Basic::peek() const { return pos_ < len_ ? src_[pos_] : '\0'; }

bool Basic::matchKw(const char* kw) {
    std::size_t i = 0;
    while (kw[i]) {
        if (pos_ + i >= len_) return false;
        char c = src_[pos_ + i];
        if (c >= 'a' && c <= 'z') c = char(c - 32);
        if (c != kw[i]) return false;
        ++i;
    }
    pos_ += i;
    return true;
}
bool Basic::parseLineNumber(int& out) {
    skipSpaces();
    if (atEnd() || !std::isdigit((unsigned char)peek())) return false;
    int n = 0;
    while (!atEnd() && std::isdigit((unsigned char)peek())) n = n * 10 + (src_[pos_++] - '0');
    out = n; return true;
}
std::string Basic::parseName(uint8_t& type) {
    std::string name;
    while (!atEnd() && std::isalnum((unsigned char)peek())) name += src_[pos_++];
    type = VT_NUM;
    if      (peek() == '$') { type = VT_STR; ++pos_; }
    else if (peek() == '%') { type = VT_INT; ++pos_; }
    return name;
}


} // namespace apps::ghost
