#include "files.h"
#include "board/storage.h"

#include <LittleFS.h>
#include <SD_MMC.h>

#include <cstdio>
#include <cstring>

namespace core::files {

namespace {

constexpr const char* ROOT  = "/GHOST";
constexpr const char* BASIC = "/GHOST/BASIC";
constexpr const char* DATA  = "/GHOST/DATA";
constexpr const char* SYS   = "/GHOST/SYS";
constexpr const char* APPS  = "/GHOST/APPS";

fs::FS* s_fs = nullptr;
bool    s_sd = false;

void ensureDir(const char* path) {
    if (!s_fs) return;
    if (!s_fs->exists(path)) s_fs->mkdir(path);
}

void makeLayout() {
    ensureDir(ROOT);
    ensureDir(BASIC);
    ensureDir(DATA);
    ensureDir(SYS);
    ensureDir(APPS);
}

// Depth-first delete of everything under `path` (the entry itself included
// when `self` is set). Arduino's FS has no recursive remove.
void removeTree(const char* path, bool self) {
    if (!s_fs) return;
    File dir = s_fs->open(path);
    if (!dir) return;

    if (!dir.isDirectory()) {
        dir.close();
        s_fs->remove(path);
        return;
    }

    for (File e = dir.openNextFile(); e; e = dir.openNextFile()) {
        char child[192];
        std::snprintf(child, sizeof child, "%s%s%s", path,
                      path[std::strlen(path) - 1] == '/' ? "" : "/", e.name());
        const bool isDir = e.isDirectory();
        e.close();
        if (isDir) removeTree(child, true);
        else       s_fs->remove(child);
    }
    dir.close();
    if (self) s_fs->rmdir(path);
}

} // namespace

bool begin() {
    if (board::storage::sdOk())         { s_fs = board::storage::sd();    s_sd = true;  }
    else if (board::storage::flashOk()) { s_fs = board::storage::flash(); s_sd = false; }
    else                                { s_fs = nullptr; return false; }
    makeLayout();
    return true;
}

bool ready()    { return s_fs != nullptr; }
bool usingSD()  { return s_sd; }
fs::FS* fs()    { return s_fs; }

const char* root()     { return ROOT; }
const char* basicDir() { return BASIC; }
const char* dataDir()  { return DATA; }
const char* sysDir()   { return SYS; }

bool appDir(const char* app, char* out, std::size_t n) {
    if (!s_fs || !app || !*app) return false;
    std::snprintf(out, n, "%s/%s", APPS, app);
    ensureDir(APPS);
    if (!s_fs->exists(out)) s_fs->mkdir(out);
    return s_fs->exists(out);
}

uint64_t totalBytes() {
    if (!s_fs) return 0;
    return s_sd ? SD_MMC.totalBytes() : LittleFS.totalBytes();
}

uint64_t usedBytes() {
    if (!s_fs) return 0;
    return s_sd ? SD_MMC.usedBytes() : LittleFS.usedBytes();
}

bool wipe() {
    if (!s_fs) return false;
    removeTree("/", false);   // clear the volume, keep the root itself
    makeLayout();
    return true;
}

} // namespace core::files
