#include "display.h"
#include "pins.h"

#include <Arduino_GFX_Library.h>
#include <esp_heap_caps.h>

namespace board::display {

namespace {

Arduino_DataBus*  s_bus   = nullptr;
Arduino_SH8601*   s_panel = nullptr;
uint16_t*         s_fb    = nullptr;

// Allocate the QSPI bus and the SH8601 panel object at the earliest user
// priority. Doing it here, instead of as plain namespace-scope globals or
// inside begin(), keeps the constructor running before arduino-esp32 has
// a chance to claim GPIO 11/12 (SPI2 default pins) via its peripheral
// manager. Without this priority, splitting the project into multiple .cpp
// files would cause spi_bus_initialize() to fail with ESP_ERR_INVALID_STATE.
__attribute__((constructor(101)))
void allocPanelObjects() {
    s_bus = new Arduino_ESP32QSPI(
        pins::LCD_CS, pins::LCD_SCK,
        pins::LCD_D0, pins::LCD_D1, pins::LCD_D2, pins::LCD_D3);
    s_panel = new Arduino_SH8601(
        s_bus, GFX_NOT_DEFINED /* RST */, 0 /* rotation */, WIDTH, HEIGHT);
}

} // namespace

bool begin(uint32_t pclk_hz) {
    if (!s_panel || !s_panel->begin(pclk_hz)) return false;
    s_panel->fillScreen(0x0000);
    s_panel->setBrightness(255);

    s_fb = static_cast<uint16_t*>(heap_caps_aligned_alloc(
        16, size_t(WIDTH) * HEIGHT * sizeof(uint16_t),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    return s_fb != nullptr;
}

gfx::Surface canvas() {
    return gfx::Surface{s_fb, WIDTH, HEIGHT};
}

void present() {
    if (!s_panel || !s_fb) return;
    s_panel->startWrite();
    s_panel->writeAddrWindow(0, 0, WIDTH, HEIGHT);
    s_bus->writePixels(s_fb, size_t(WIDTH) * HEIGHT);
    s_panel->endWrite();
}

void setBrightness(uint8_t value) {
    if (s_panel) s_panel->setBrightness(value);
}

} // namespace board::display
