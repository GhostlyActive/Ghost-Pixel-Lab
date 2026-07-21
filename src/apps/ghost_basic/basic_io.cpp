// Ghost BASIC — everything that talks to storage or a device: SAVE, LOAD,
// DIRECTORY, SCRATCH and VERIFY for whole programs, plus the logical-file
// statements OPEN / CLOSE / PRINT# / INPUT# / GET# / CMD for data files.
//
// Kept apart from basic.cpp so the run loop and the language stay readable;
// this is the only part of the interpreter that can fail because of the world
// outside it — a missing file, a full disk, no drive at all.
#include "basic.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace apps::ghost {

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
    skipDeviceSuffix();
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
    skipDeviceSuffix();
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


Basic::OpenFile* Basic::findFile(int lf) {
    for (auto& f : openFiles_) if (f.used && f.lf == lf) return &f;
    return nullptr;
}

// A 1541 file name may carry ",S,W" or ",P,R" type and mode suffixes. The bare
// name is everything before the first comma; the suffix decides read vs write.
void Basic::stOpen() {
    Value lfv = parseExpr(); if (err_) return;
    int dev = 8, sa = 0;
    std::string spec;
    skipSpaces();
    if (peek() == ',') { ++pos_; Value d = parseExpr(); if (err_) return; dev = int(d.num); }
    skipSpaces();
    if (peek() == ',') { ++pos_; Value a = parseExpr(); if (err_) return; sa = int(a.num); }
    skipSpaces();
    if (peek() == ',') { ++pos_; Value n = parseExpr(); if (err_) return;
                         if (!n.isStr) { setError("TYPE MISMATCH"); return; }
                         spec = n.str; }

    const int lf = int(lfv.num);
    if (lf <= 0 || lf > 255) { setError("ILLEGAL QUANTITY"); return; }
    if (findFile(lf))        { setError("FILE OPEN"); return; }

    OpenFile* slot = nullptr;
    for (auto& f : openFiles_) if (!f.used) { slot = &f; break; }
    if (!slot) { setError("TOO MANY FILES"); return; }

    std::string name = spec.substr(0, spec.find(','));
    for (char& c : name) if (c >= 'a' && c <= 'z') c = char(c - 32);
    if (name.size() > 16) name.resize(16);

    bool write = (sa == 1);
    if (spec.find(",W") != std::string::npos) write = true;
    if (spec.find(",R") != std::string::npos) write = false;

    *slot = OpenFile{};
    slot->lf = lf; slot->dev = dev; slot->sa = sa;
    slot->write = write; slot->used = true; slot->name = name;

    st_ = 0;
    if (!write) {
        if (name.empty()) { setError("MISSING FILE NAME"); return; }
        if (!files_) { setError("DEVICE NOT PRESENT"); return; }
        if (!files_->load(name.c_str(), slot->buf)) { setError("FILE NOT FOUND"); return; }
    }
}

void Basic::stClose() {
    Value lfv = parseExpr(); if (err_) return;
    const int lf = int(lfv.num);
    OpenFile* f = findFile(lf);
    if (!f) return;                       // closing an unopened file is harmless
    if (f->write) {
        if (!files_) { setError("DEVICE NOT PRESENT"); f->used = false; return; }
        if (f->name.empty()) { setError("MISSING FILE NAME"); f->used = false; return; }
        files_->save(f->name.c_str(), f->buf);
    }
    if (cmdLf_ == lf) { cmdLf_ = -1; printTo_ = -1; }
    f->used = false;
}

// PRINT# lf, ... — the same printing code, aimed at a file for one statement.
void Basic::stPrintFile() {
    Value lfv = parseExpr(); if (err_) return;
    OpenFile* f = findFile(int(lfv.num));
    if (!f) { setError("FILE NOT OPEN"); return; }
    skipSpaces();
    if (peek() == ',') ++pos_;
    printTo_ = f->lf;
    stPrint();
    printTo_ = cmdLf_;
}

void Basic::stInputFile() {
    Value lfv = parseExpr(); if (err_) return;
    OpenFile* f = findFile(int(lfv.num));
    if (!f) { setError("FILE NOT OPEN"); return; }
    skipSpaces();
    if (peek() == ',') ++pos_;

    // One line from the file, split on commas across the variable list.
    std::string lineText;
    if (f->pos >= f->buf.size()) st_ = 64;          // 64 == end of file
    while (f->pos < f->buf.size() && f->buf[f->pos] != 0x0D && f->buf[f->pos] != '\n')
        lineText += f->buf[f->pos++];
    if (f->pos < f->buf.size()) ++f->pos;           // step over the terminator
    if (f->pos >= f->buf.size()) st_ = 64;

    std::size_t at = 0;
    for (;;) {
        skipSpaces();
        std::string field;
        if (at <= lineText.size()) {
            const std::size_t comma = lineText.find(',', at);
            field = lineText.substr(at, comma == std::string::npos ? std::string::npos : comma - at);
            at = comma == std::string::npos ? lineText.size() + 1 : comma + 1;
        }
        const std::size_t b = field.find_first_not_of(' ');
        const std::size_t e = field.find_last_not_of(' ');
        field = (b == std::string::npos) ? std::string() : field.substr(b, e - b + 1);
        assignTarget(Value::string(field));
        if (err_) return;
        skipSpaces();
        if (peek() == ',') { ++pos_; continue; }
        break;
    }
}

void Basic::stGetFile() {
    Value lfv = parseExpr(); if (err_) return;
    OpenFile* f = findFile(int(lfv.num));
    if (!f) { setError("FILE NOT OPEN"); return; }
    skipSpaces();
    if (peek() == ',') ++pos_;

    std::string one;
    if (f->pos < f->buf.size()) one = std::string(1, f->buf[f->pos++]);
    if (f->pos >= f->buf.size()) st_ = 64;
    assignTarget(Value::string(one));
}

// CMD lf — send everything PRINT produces to the file until it is closed.
void Basic::stCmd() {
    Value lfv = parseExpr(); if (err_) return;
    OpenFile* f = findFile(int(lfv.num));
    if (!f) { setError("FILE NOT OPEN"); return; }
    cmdLf_ = f->lf;
    printTo_ = f->lf;
}

void Basic::stVerify() {
    std::string name;
    if (!parseFileName(name)) return;
    skipDeviceSuffix();
    if (!files_) { setError("DEVICE NOT PRESENT"); return; }
    std::string onDisk;
    if (!files_->load(name.c_str(), onDisk)) { setError("FILE NOT FOUND"); return; }

    std::string here;
    for (const auto& ln : lines_) {
        const std::string num = formatNumber(ln.num);
        here += num.c_str() + 1;
        here += ' ';
        here += ln.src;
        here += '\n';
    }
    if (here != onDisk) { setError("VERIFY"); return; }
    outText("OK");
    outChar(0x0D);
}

// The ",8" or ",8,1" a real listing puts after a file name. We have one drive,
// so the numbers are accepted and ignored rather than rejected.
void Basic::skipDeviceSuffix() {
    for (;;) {
        skipSpaces();
        if (peek() != ',') return;
        ++pos_;
        Value ignored = parseExpr();
        (void)ignored;
        if (err_) return;
    }
}

} // namespace apps::ghost
