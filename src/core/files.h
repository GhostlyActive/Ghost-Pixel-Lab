// Shared filesystem layout for the whole device. Picks the SD card when one
// is inserted and falls back to internal flash, so apps never have to care
// which one they got.
//
// Layout (mirrors how a C64 user thought about a disk — one place for
// programs, one for data, one for system settings):
//
//   /GHOST/BASIC   BASIC programs, "NAME.PRG"   (SAVE / LOAD / DIRECTORY)
//   /GHOST/DATA    data files an app writes
//   /GHOST/SYS     system settings (paired keyboard, ...)
//   /GHOST/APPS/<app>   one private folder per app
//
// Call begin() once at boot, after board::storage.
#pragma once

#include <FS.h>
#include <cstddef>

namespace core::files {

bool begin();                    // choose the device and create the layout
[[nodiscard]] bool ready();
[[nodiscard]] bool usingSD();    // false when we fell back to internal flash

fs::FS* fs();                    // nullptr when no filesystem at all

const char* root();              // "/GHOST"
const char* basicDir();          // "/GHOST/BASIC"
const char* dataDir();           // "/GHOST/DATA"
const char* sysDir();            // "/GHOST/SYS"

// Private folder for one app, created on demand: "/GHOST/APPS/<name>".
// Returns false if it could not be created.
bool appDir(const char* app, char* out, std::size_t n);

// Capacity of the active device, in bytes. 0 when unknown.
uint64_t totalBytes();
uint64_t usedBytes();

// DESTRUCTIVE: delete every file and folder on the active device, then
// recreate the layout above. This is what the "format" button calls.
bool wipe();

} // namespace core::files
