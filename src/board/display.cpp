#include "display.h"
#include "pins.h"

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <esp_heap_caps.h>
#include <driver/spi_master.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

namespace board::display {

namespace {

constexpr size_t FB_BYTES    = size_t(WIDTH) * HEIGHT * sizeof(uint16_t);
// Per SPI transaction. The framebuffer lives in PSRAM, so the driver copies
// each chunk through an internal DMA bounce buffer it allocates per transfer.
// 4 KB rather than 16 KB because that allocation has to succeed against a heap
// that BLE and the audio I2S buffers have already fragmented: entering Outer
// Pixels after a sound app left the largest free DMA block at ~14 KB, and every
// 16 KB request failed for as long as the app was open.
constexpr size_t CHUNK_BYTES = 4 * 1024;
constexpr int    QUEUE_DEPTH = 4;

#if BOARD_V2
using PanelDriver = Arduino_CO5300;
#else
using PanelDriver = Arduino_SH8601;
#endif

Arduino_DataBus* s_bus   = nullptr;
PanelDriver*     s_panel = nullptr;

// Double buffer: the app draws into s_fb[s_drawIdx] while the presenter
// task streams the other buffer to the panel.
uint16_t* s_fb[2]   = {nullptr, nullptr};
int       s_drawIdx = 0;

spi_device_handle_t s_pixDev      = nullptr;  // our own queued-DMA device
TaskHandle_t        s_presentTask = nullptr;
SemaphoreHandle_t   s_presentIdle = nullptr;  // given = transfer finished
uint16_t* volatile  s_pendingFb   = nullptr;

// Allocate the QSPI bus and the panel object at the earliest user
// priority. Doing it here, instead of as plain namespace-scope globals or
// inside begin(), keeps the constructor running before arduino-esp32 has
// a chance to claim GPIO 11/12 (SPI2 default pins) via its peripheral
// manager. Without this priority, splitting the project into multiple .cpp
// files would cause spi_bus_initialize() to fail with ESP_ERR_INVALID_STATE.
__attribute__((constructor(101)))
void allocPanelObjects() {
    // 'true' = shared interface: the library acquires the bus per write
    // instead of permanently, so the pixel-streaming device below can
    // coexist on the same SPI host.
    s_bus = new Arduino_ESP32QSPI(
        pins::LCD_CS, pins::LCD_SCK,
        pins::LCD_D0, pins::LCD_D1, pins::LCD_D2, pins::LCD_D3,
        /*is_shared_interface=*/true);
#if BOARD_V2
    // The CO5300's visible 368 columns start at panel RAM column 16. Without
    // that offset the image is shifted and wraps at the edge.
    s_panel = new PanelDriver(
        s_bus, GFX_NOT_DEFINED /* RST */, 0 /* rotation */, WIDTH, HEIGHT,
        16 /* col_offset1 */, 0, 0, 0);
#else
    s_panel = new PanelDriver(
        s_bus, GFX_NOT_DEFINED /* RST */, 0 /* rotation */, WIDTH, HEIGHT);
#endif
}

// Stream one framebuffer to the panel, mirroring the wire protocol of
// Arduino_ESP32QSPI::writeBytes (identical on SH8601 and CO5300): first chunk cmd 0x32 / addr 0x003C00
// (write-continue), remaining chunks raw QIO data. Queued (interrupt)
// transactions instead of the library's busy-polling: the task sleeps
// during the wire time and core 0's idle task keeps the watchdog fed.
void streamFrame(const uint8_t* data) {
    static spi_transaction_ext_t trans[QUEUE_DEPTH];

    spi_device_acquire_bus(s_pixDev, portMAX_DELAY);
    digitalWrite(pins::LCD_CS, LOW);

    size_t remaining = FB_BYTES;
    size_t offset    = 0;
    int    queued    = 0;
    int    slot      = 0;
    bool   first     = true;
    while (remaining > 0 || queued > 0) {
        if (queued == QUEUE_DEPTH || remaining == 0) {
            spi_transaction_t* done;
            spi_device_get_trans_result(s_pixDev, &done, portMAX_DELAY);
            --queued;
            continue;
        }
        const size_t n = remaining < CHUNK_BYTES ? remaining : CHUNK_BYTES;
        auto& t = trans[slot];
        t = {};
        if (first) {
            t.base.flags = SPI_TRANS_MODE_QIO;
            t.base.cmd   = 0x32;
            t.base.addr  = 0x003C00;
            first = false;
        } else {
            // Zero-length cmd/addr/dummy phases (set via the ext fields,
            // which t = {} cleared) — data-only continuation.
            t.base.flags = SPI_TRANS_MODE_QIO | SPI_TRANS_VARIABLE_CMD |
                           SPI_TRANS_VARIABLE_ADDR | SPI_TRANS_VARIABLE_DUMMY;
        }
        t.base.tx_buffer = data + offset;
        t.base.length    = n * 8;
        const esp_err_t err = spi_device_queue_trans(s_pixDev, &t.base, portMAX_DELAY);
        if (err != ESP_OK) {
            // Out of DMA memory for the driver's bounce copy. Give up on the
            // rest of this frame, but keep looping so the transactions already
            // in flight are still reaped: leaving results unclaimed desyncs the
            // driver's queue and every later frame inherits the damage.
            //
            // The number that matters is the largest free block, not the total
            // — this fails on a fragmented heap with plenty free overall.
            static uint32_t lastWarnMs = 0;
            if (millis() - lastWarnMs > 1000) {
                lastWarnMs = millis();
                Serial.printf("[display] chunk of %u B rejected (%d), largest free DMA block %u B\n",
                              (unsigned)CHUNK_BYTES, err,
                              (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA));
            }
            remaining = 0;
            continue;
        }
        slot = (slot + 1) % QUEUE_DEPTH;
        ++queued;
        offset    += n;
        remaining -= n;
    }

    digitalWrite(pins::LCD_CS, HIGH);
    spi_device_release_bus(s_pixDev);
}

void presentTaskFn(void*) {
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        const auto* fb = reinterpret_cast<const uint8_t*>(s_pendingFb);
        s_panel->startWrite();
        s_panel->writeAddrWindow(0, 0, WIDTH, HEIGHT);
        s_panel->endWrite();
        streamFrame(fb);
        xSemaphoreGive(s_presentIdle);
    }
}

} // namespace

bool begin(uint32_t pclk_hz) {
    if (!s_panel || !s_panel->begin(pclk_hz)) return false;
    s_panel->fillScreen(0x0000);
    s_panel->setBrightness(255);

    // 64-byte (cache line) alignment so the SPI driver may DMA straight
    // from PSRAM where it supports that.
    for (auto& fb : s_fb) {
        fb = static_cast<uint16_t*>(heap_caps_aligned_alloc(
            64, FB_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (!fb) return false;
    }

    // Second device on the SPI2 host, used only for the pixel stream.
    spi_device_interface_config_t devcfg = {};
    devcfg.command_bits   = 8;
    devcfg.address_bits   = 24;
    devcfg.mode           = SPI_MODE0;
    devcfg.clock_speed_hz = static_cast<int>(pclk_hz);
    devcfg.spics_io_num   = -1;  // CS driven manually, like the library does
    devcfg.flags          = SPI_DEVICE_HALFDUPLEX;
    devcfg.queue_size     = QUEUE_DEPTH;
    if (spi_bus_add_device(SPI2_HOST, &devcfg, &s_pixDev) != ESP_OK) return false;

    s_presentIdle = xSemaphoreCreateBinary();
    if (!s_presentIdle) return false;
    xSemaphoreGive(s_presentIdle);

    // Core 1 runs the Arduino loop; the presenter lives on core 0 so the
    // panel transfer overlaps the next frame's drawing.
    return xTaskCreatePinnedToCore(presentTaskFn, "present", 4096, nullptr,
                                   5, &s_presentTask, 0) == pdPASS;
}

gfx::Surface canvas() {
    return gfx::Surface{s_fb[s_drawIdx], WIDTH, HEIGHT};
}

void present() {
    if (!s_presentTask) return;
    xSemaphoreTake(s_presentIdle, portMAX_DELAY);  // previous frame done
    s_pendingFb = s_fb[s_drawIdx];
    s_drawIdx ^= 1;
    xTaskNotifyGive(s_presentTask);
}

void setBrightness(uint8_t value) {
    if (!s_panel) return;
    // Exclude the presenter: the idle semaphore doubles as a bus lock.
    if (s_presentIdle) xSemaphoreTake(s_presentIdle, portMAX_DELAY);
    s_panel->setBrightness(value);
    if (s_presentIdle) xSemaphoreGive(s_presentIdle);
}

} // namespace board::display
