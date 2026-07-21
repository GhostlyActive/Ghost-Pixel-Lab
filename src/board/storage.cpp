#include "storage.h"
#include "pins.h"

#include <LittleFS.h>
#include <SD_MMC.h>
#include <esp_heap_caps.h>

namespace board::storage {

namespace {
bool s_flash = false;
bool s_sd    = false;
} // namespace

bool beginFlash() {
    if (s_flash) return true;
    s_flash = LittleFS.begin(true /*format on fail*/);
    return s_flash;
}

bool beginSD() {
    if (s_sd) return true;
    SD_MMC.setPins(pins::SD_CLK, pins::SD_CMD, pins::SD_DATA);
    // format_if_mount_failed only helps a card that answers but carries no
    // usable filesystem; it cannot rescue a card that stays silent.
    // A freshly powered card may need a moment before it replies to ACMD41,
    // and a marginal contact often succeeds on a later try — so retry a few
    // times at the conservative 20 MHz clock instead of giving up at once.
    for (int attempt = 0; attempt < 3 && !s_sd; ++attempt) {
        if (attempt) delay(150);
        s_sd = SD_MMC.begin("/sdcard", true /*1-bit bus*/,
                            true /*format if unmountable*/, SDMMC_FREQ_DEFAULT);
    }
    return s_sd;
}

bool flashOk() { return s_flash; }
bool sdOk()    { return s_sd; }

fs::FS* flash() { return s_flash ? static_cast<fs::FS*>(&LittleFS) : nullptr; }
fs::FS* sd()    { return s_sd    ? static_cast<fs::FS*>(&SD_MMC)   : nullptr; }

uint8_t* loadToPsram(fs::FS& f, const char* path, size_t& size) {
    size = 0;
    File file = f.open(path, "r");
    if (!file || file.isDirectory()) return nullptr;
    const size_t n = file.size();
    auto* buf = static_cast<uint8_t*>(heap_caps_malloc(n, MALLOC_CAP_SPIRAM));
    if (!buf) { file.close(); return nullptr; }
    const bool ok = file.read(buf, n) == n;
    file.close();
    if (!ok) { free(buf); return nullptr; }
    size = n;
    return buf;
}

} // namespace board::storage
