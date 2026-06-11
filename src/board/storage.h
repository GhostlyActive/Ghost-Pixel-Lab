// Filesystems: internal flash (LittleFS data partition, upload assets with
// `pio run -t uploadfs` from the data/ folder) and the micro-SD slot
// (SD_MMC, 1-bit bus). Both expose the standard Arduino fs::FS API.
#pragma once

#include <FS.h>
#include <cstddef>
#include <cstdint>

namespace board::storage {

bool beginFlash();  // LittleFS; formats the partition on first use
bool beginSD();     // false when no card is inserted

[[nodiscard]] bool flashOk();
[[nodiscard]] bool sdOk();

fs::FS* flash();    // nullptr if not mounted
fs::FS* sd();

// Read a whole file into PSRAM. Caller free()s. nullptr on error.
uint8_t* loadToPsram(fs::FS& f, const char* path, size_t& size);

} // namespace board::storage
