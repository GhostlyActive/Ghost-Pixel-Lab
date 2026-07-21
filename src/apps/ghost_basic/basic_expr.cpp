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
    if (matchKw("FN")) { skipSpaces(); bool s; std::string nm = parseName(s); return callFn(nm); }
    if (std::isalpha((unsigned char)c)) {
        bool isStr = false;
        std::string name = parseName(isStr);
        const std::string fname = isStr ? name + "$" : name;   // functions keep the $
        skipSpaces();
        if (peek() == '(') {
            if (isFunction(fname)) return callFunction(fname);
            return getArr(name, isStr);
        }
        return getVar(name, isStr);
    }
    setError("SYNTAX");
    return Value::number(0);
}

bool Basic::isFunction(const std::string& n) {
    static const char* F[] = {"ABS","INT","SGN","SQR","SIN","COS","TAN","ATN","EXP",
        "LOG","RND","LEN","ASC","VAL","PEEK","CHR$","STR$","LEFT$","RIGHT$","MID$"};
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
    else if (name == "SQR") r = Value::number(std::sqrt(a.num));
    else if (name == "SIN") r = Value::number(std::sin(a.num));
    else if (name == "COS") r = Value::number(std::cos(a.num));
    else if (name == "TAN") r = Value::number(std::tan(a.num));
    else if (name == "ATN") r = Value::number(std::atan(a.num));
    else if (name == "EXP") r = Value::number(std::exp(a.num));
    else if (name == "LOG") r = Value::number(std::log(a.num));
    else if (name == "PEEK") r = Value::number(peekMem(int(a.num)));
    else if (name == "LEN") r = Value::number(double(a.str.size()));
    else if (name == "ASC") r = Value::number(a.str.empty() ? 0 : (unsigned char)a.str[0]);
    else if (name == "VAL") r = Value::number(std::atof(a.str.c_str()));
    else if (name == "CHR$") r = Value::string(std::string(1, char(int(a.num) & 0xFF)));
    else if (name == "STR$") r = Value::string(formatNumber(a.num));
    else if (name == "RND") { rngState_ = rngState_ * 1103515245u + 12345u;
        r = Value::number(((rngState_ >> 16) & 0x7FFF) / 32768.0); }
    else if (name == "LEFT$") { Value n = nextArg(); if (err_) return r;
        r = Value::string(a.str.substr(0, std::size_t(std::max(0, int(n.num))))); }
    else if (name == "RIGHT$") { Value n = nextArg(); if (err_) return r;
        int k = std::max(0, int(n.num)); int st = std::max(0, int(a.str.size()) - k);
        r = Value::string(a.str.substr(std::size_t(st))); }
    else if (name == "MID$") { Value s = nextArg(); if (err_) return r;
        int start = std::max(1, int(s.num)) - 1; int cnt = int(a.str.size());
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
    Value saved = getVar(pname, false);
    setVar(pname, false, arg);

    const char* prevSrc = src_; std::size_t prevPos = pos_, prevLen = len_;
    std::string body = fn->expr;
    src_ = body.c_str(); len_ = body.size(); pos_ = 0;
    Value res = parseExpr();
    src_ = prevSrc; len_ = prevLen; pos_ = prevPos;

    setVar(pname, false, saved);
    return res;
}

// ---------------------------------------------------------------------------
// variables & arrays
// ---------------------------------------------------------------------------

Basic::Value Basic::getVar(const std::string& name, bool str) {
    char a = name.size() ? name[0] : ' ', b = name.size() > 1 ? name[1] : ' ';
    Var* v = findVar(a, b, str);
    if (v) return v->val;
    return str ? Value::string("") : Value::number(0);
}
Basic::Var* Basic::findVar(char a, char b, bool str) {
    for (auto& v : vars_) if (v.n0 == a && v.n1 == b && v.str == str) return &v;
    return nullptr;
}
void Basic::setVar(const std::string& name, bool str, const Value& val) {
    char a = name.size() ? name[0] : ' ', b = name.size() > 1 ? name[1] : ' ';
    Var* v = findVar(a, b, str);
    if (v) { v->val = val; return; }
    vars_.push_back(Var{a, b, str, val});
}

Basic::Arr* Basic::findArr(char a, char b, bool str) {
    for (auto& x : arrays_) if (x.n0 == a && x.n1 == b && x.str == str) return &x;
    return nullptr;
}
Basic::Arr& Basic::ensureArr(const std::string& name, bool str, int ndims) {
    char a = name.size() ? name[0] : ' ', b = name.size() > 1 ? name[1] : ' ';
    Arr* e = findArr(a, b, str);
    if (e) return *e;
    Arr arr; arr.n0 = a; arr.n1 = b; arr.str = str;
    int total = 1;
    for (int i = 0; i < ndims; ++i) { arr.dim.push_back(11); total *= 11; }  // auto-dim 0..10
    if (str) arr.s.assign(total, std::string()); else arr.num.assign(total, 0.0);
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
Basic::Value Basic::getArr(const std::string& name, bool str) {
    std::vector<int> idx = parseIndices();
    if (err_) return Value::number(0);
    Arr& a = ensureArr(name, str, int(idx.size()));
    int f = flatIndex(a, idx);
    if (err_) return Value::number(0);
    return str ? Value::string(a.s[f]) : Value::number(a.num[f]);
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
std::string Basic::parseName(bool& isStr) {
    std::string name;
    while (!atEnd() && std::isalnum((unsigned char)peek())) name += src_[pos_++];
    isStr = false;
    if (peek() == '$') { isStr = true; ++pos_; }
    return name;
}


} // namespace apps::ghost
