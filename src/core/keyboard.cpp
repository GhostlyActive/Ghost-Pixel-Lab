#include "keyboard.h"
#include "files.h"
#include "config.h"

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace core::keyboard {

namespace {

constexpr uint16_t HID_SERVICE = 0x1812;

constexpr int MAX_DEVICES = 64;

SemaphoreHandle_t s_lock = nullptr;
TaskHandle_t      s_task = nullptr;
NimBLEClient*     s_client = nullptr;

volatile bool s_connected = false;
volatile bool s_scanActive = false;

// Discovered devices (guarded by s_lock).
Device s_devices[MAX_DEVICES];
int    s_deviceCount = 0;
int    s_seenTotal   = 0;   // unique addresses seen, including evicted ones

// How much we want to keep an entry when the buffer is full. A keyboard beats
// a nameless beacon every time, so saturation can never hide the one device
// the user is actually looking for.
int score(const Device& d) {
    return (d.hid ? 400 : 0) + (d.appearance == 0x03C1 ? 200 : 0) +
           (d.connectable ? 100 : 0) + (d.name[0] ? 50 : 0) + (d.rssi + 110);
}

// The remembered keyboard.
bool          s_haveSaved = false;
NimBLEAddress s_savedAddr;
char          s_savedName[26] = {0};
char          s_savedAddrStr[20] = {0};

// A device the user tapped, being probed before we commit to it.
volatile bool s_probe = false;
PairState     s_pairState = PairState::Idle;
NimBLEAddress s_candAddr;
char          s_candAddrStr[20] = {0};
char          s_candName[26] = {0};
uint8_t       s_candType = 0;

// Decoded-key ring buffer.
constexpr int RING = 32;
uint8_t       s_ring[RING];
volatile int  s_head = 0, s_tail = 0;

uint8_t s_prev[6] = {0};

// ---------------------------------------------------------------------------
// persistence
// ---------------------------------------------------------------------------

fs::FS* store() { return core::files::fs(); }

// /GHOST/SYS/KEYBOARD.TXT — one line: "addr,type,name"
const char* storePath() {
    static char p[96];
    std::snprintf(p, sizeof p, "%s/KEYBOARD.TXT", core::files::sysDir());
    return p;
}

void savePairing(const char* addr, uint8_t type, const char* name) {
    fs::FS* fs = store();
    if (!fs) return;
    File f = fs->open(storePath(), "w");
    if (!f) return;
    f.printf("%s,%u,%s\n", addr, unsigned(type), name);
    f.close();
}

void loadPairing() {
    fs::FS* fs = store();
    if (!fs) return;
    File f = fs->open(storePath(), "r");
    if (!f) return;
    char buf[80] = {0};
    const size_t n = f.readBytesUntil('\n', buf, sizeof(buf) - 1);
    f.close();
    buf[n] = 0;
    if (!n) return;

    // "aa:bb:cc:dd:ee:ff,type,name"
    char* c1 = std::strchr(buf, ',');
    if (!c1) return;
    *c1 = 0;
    char* c2 = std::strchr(c1 + 1, ',');
    const uint8_t type = uint8_t(std::atoi(c1 + 1));
    const char* name = c2 ? c2 + 1 : "";

    std::snprintf(s_savedAddrStr, sizeof s_savedAddrStr, "%s", buf);
    std::snprintf(s_savedName, sizeof s_savedName, "%s", name);
    s_savedAddr = NimBLEAddress(std::string(buf), type);
    s_haveSaved = true;
}

// ---------------------------------------------------------------------------
// key decoding
// ---------------------------------------------------------------------------

void ringPush(uint8_t k) {
    xSemaphoreTake(s_lock, portMAX_DELAY);
    int n = (s_head + 1) % RING;
    if (n != s_tail) { s_ring[s_head] = k; s_head = n; }
    xSemaphoreGive(s_lock);
}

uint8_t decode(uint8_t u, bool shift) {
    if (u >= 0x04 && u <= 0x1D) return uint8_t('A' + (u - 0x04));
    switch (u) {
    case 0x1E: return shift ? '!' : '1';
    case 0x1F: return shift ? '"' : '2';
    case 0x20: return shift ? '#' : '3';
    case 0x21: return shift ? '$' : '4';
    case 0x22: return shift ? '%' : '5';
    case 0x23: return shift ? '&' : '6';
    case 0x24: return shift ? '\'' : '7';
    case 0x25: return shift ? '(' : '8';
    case 0x26: return shift ? ')' : '9';
    case 0x27: return '0';
    case 0x28: return 0x0D;               // Enter
    case 0x29: return 0x03;               // Esc -> RUN/STOP
    case 0x2A: return 0x14;               // Backspace -> DELETE
    case 0x2C: return ' ';
    case 0x2D: return shift ? '=' : '-';
    case 0x2E: return shift ? '+' : '=';
    case 0x2F: return '[';
    case 0x30: return ']';
    case 0x33: return shift ? ':' : ';';
    case 0x34: return shift ? '"' : '\'';
    case 0x35: return '^';                // exponent
    case 0x36: return shift ? '<' : ',';
    case 0x37: return shift ? '>' : '.';
    case 0x38: return shift ? '?' : '/';
    case 0x4A: return 0x13;               // Home
    case 0x4F: return 0x1D;               // Right
    case 0x50: return 0x9D;               // Left
    case 0x51: return 0x11;               // Down
    case 0x52: return 0x91;               // Up
    default:   return 0;
    }
}

void handleReport(const uint8_t* r) {
    const bool shift = (r[0] & 0x22) != 0;
    for (int i = 2; i < 8; ++i) {
        const uint8_t u = r[i];
        if (!u) continue;
        bool wasDown = false;
        for (int j = 0; j < 6; ++j) if (s_prev[j] == u) { wasDown = true; break; }
        if (!wasDown) { uint8_t k = decode(u, shift); if (k) ringPush(k); }
    }
    for (int i = 0; i < 6; ++i) s_prev[i] = r[2 + i];
}

void onNotify(NimBLERemoteCharacteristic*, uint8_t* data, size_t len, bool) {
    const uint8_t* r = nullptr;
    if      (len == 8) r = data;
    else if (len == 9) r = data + 1;    // report-id prefixed
    if (r) handleReport(r);
}

// ---------------------------------------------------------------------------
// BLE callbacks
// ---------------------------------------------------------------------------

class ClientCB : public NimBLEClientCallbacks {
    void onDisconnect(NimBLEClient*, int) override {
        s_connected = false;
        for (int i = 0; i < 6; ++i) s_prev[i] = 0;
    }
    void onConfirmPasskey(NimBLEConnInfo& info, uint32_t) override {
        NimBLEDevice::injectConfirmPasskey(info, true);
    }
} s_clientCb;

class ScanCB : public NimBLEScanCallbacks {
    void onResult(const NimBLEAdvertisedDevice* d) override {
        const std::string addr = d->getAddress().toString();
        const uint16_t ap = d->getAppearance();
        const bool hid = d->isAdvertisingService(NimBLEUUID(HID_SERVICE)) ||
                         (ap >= 0x03C0 && ap <= 0x03C2);

        xSemaphoreTake(s_lock, portMAX_DELAY);
        int slot = -1;
        for (int i = 0; i < s_deviceCount; ++i)
            if (std::strcmp(s_devices[i].addr, addr.c_str()) == 0) { slot = i; break; }

        if (slot < 0) {
            ++s_seenTotal;
            if (s_deviceCount < MAX_DEVICES) {
                slot = s_deviceCount++;
            } else {
                // Full: throw out the least interesting entry, but only if the
                // newcomer is actually better than it.
                Device cand{};
                cand.rssi = d->getRSSI(); cand.hid = hid; cand.appearance = ap;
                cand.connectable = d->isConnectable();
                const std::string nm0 = d->getName();
                if (!nm0.empty()) cand.name[0] = 'x';
                int worst = 0, worstScore = score(s_devices[0]);
                for (int i = 1; i < s_deviceCount; ++i) {
                    const int sc = score(s_devices[i]);
                    if (sc < worstScore) { worstScore = sc; worst = i; }
                }
                if (score(cand) > worstScore) { slot = worst; s_devices[slot] = Device{}; }
            }
        }

        const bool isNew = (slot >= 0) && !s_devices[slot].addr[0];

        if (slot >= 0) {
            Device& e = s_devices[slot];
            std::snprintf(e.addr, sizeof e.addr, "%s", addr.c_str());
            e.type = d->getAddress().getType();
            const std::string nm = d->getName();   // entries start zeroed
            if (!nm.empty()) std::snprintf(e.name, sizeof e.name, "%s", nm.c_str());
            e.rssi        = d->getRSSI();
            e.appearance  = ap ? ap : e.appearance;
            e.hid         = e.hid || hid;
            e.connectable = d->isConnectable();
        }
        xSemaphoreGive(s_lock);

        // Diagnostics: one line per newly seen device, so a serial monitor
        // shows exactly what the radio hears. Printed outside the lock.
        if (isNew && config::BLE_SCAN_LOG) {
            const std::string nm = d->getName();
            Serial.printf("[ble] %s type=%u rssi=%4d conn=%d legacy=%d adv=%u "
                          "appear=0x%04X svc=%u hid=%d name=\"%s\"\n",
                          addr.c_str(), unsigned(d->getAddress().getType()),
                          int(d->getRSSI()), d->isConnectable() ? 1 : 0,
                          d->isLegacyAdvertisement() ? 1 : 0, unsigned(d->getAdvType()),
                          ap, unsigned(d->getServiceUUIDCount()), hid ? 1 : 0,
                          nm.c_str());
            for (uint8_t i = 0; i < d->getServiceUUIDCount(); ++i)
                Serial.printf("[ble]   svc[%u] = %s\n", i,
                              d->getServiceUUID(i).toString().c_str());
        }
    }
} s_scanCb;

// ---------------------------------------------------------------------------
// connect
// ---------------------------------------------------------------------------

bool connectTo(const NimBLEAddress& addr) {
    if (!s_client) s_client = NimBLEDevice::createClient(addr);
    s_client->setClientCallbacks(&s_clientCb, false);
    if (!s_client->connect(addr)) return false;
    s_client->secureConnection();

    NimBLERemoteService* hid = s_client->getService(NimBLEUUID(HID_SERVICE));
    if (!hid) { s_client->disconnect(); return false; }

    int subscribed = 0;
    for (auto* ch : hid->getCharacteristics(true))
        if (ch->canNotify() && ch->subscribe(true, onNotify)) ++subscribed;
    if (!subscribed) { s_client->disconnect(); return false; }

    s_connected = true;
    return true;
}

// Most keyboards never advertise their name; it lives in the Generic Access
// service and can only be read once connected. That is how we can label a
// device that showed up as "<no name>".
bool readGapName(char* out, size_t n) {
    if (!s_client || !s_client->isConnected()) return false;
    NimBLERemoteService* gap = s_client->getService(NimBLEUUID(uint16_t(0x1800)));
    if (!gap) return false;
    NimBLERemoteCharacteristic* ch = gap->getCharacteristic(NimBLEUUID(uint16_t(0x2A00)));
    if (!ch || !ch->canRead()) return false;
    const std::string v = std::string(ch->readValue());
    if (v.empty()) return false;
    std::snprintf(out, n, "%s", v.c_str());
    return true;
}

void task(void*) {
    for (;;) {
        // A tapped device: connect once and check it really is a keyboard.
        // connectTo() only succeeds when the HID service is present, so this
        // doubles as the "is this the right device?" test.
        if (s_probe) {
            s_probe = false;
            if (connectTo(s_candAddr)) {
                char nm[26] = {0};
                if (readGapName(nm, sizeof nm) && nm[0])
                    std::snprintf(s_savedName, sizeof s_savedName, "%s", nm);
                else
                    std::snprintf(s_savedName, sizeof s_savedName, "%s",
                                  s_candName[0] ? s_candName : s_candAddrStr);
                std::snprintf(s_savedAddrStr, sizeof s_savedAddrStr, "%s", s_candAddrStr);
                s_savedAddr = s_candAddr;
                s_haveSaved = true;
                savePairing(s_savedAddrStr, s_candType, s_savedName);
                s_pairState = PairState::Paired;
            } else {
                s_pairState = PairState::NotAKeyboard;
            }
            continue;
        }

        if (s_client && s_client->isConnected()) {
            s_connected = true;
            vTaskDelay(pdMS_TO_TICKS(300));
            continue;
        }
        s_connected = false;

        if (s_scanActive) {
            // Self-heal: a connect attempt that was still in flight when the
            // user opened the scanner suppresses scanning, and start() then
            // silently fails. Nothing would ever restart it, so the list looks
            // frozen and no new device — including the keyboard — shows up.
            NimBLEScan* scan = NimBLEDevice::getScan();
            if (!scan->isScanning()) {
                const bool ok = scan->start(0, false);
                if (config::BLE_SCAN_LOG)
                    Serial.printf("[ble] scan restart -> %s\n", ok ? "running" : "FAILED");
            }
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        if (s_haveSaved) {
            if (!connectTo(s_savedAddr)) vTaskDelay(pdMS_TO_TICKS(2000));
        } else {
            vTaskDelay(pdMS_TO_TICKS(300));
        }
    }
}

} // namespace

// ---------------------------------------------------------------------------
// public API
// ---------------------------------------------------------------------------

bool begin() {
    if (s_task) return true;
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) return false;

    NimBLEDevice::init("Ghost Pixel Lab");
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);
    NimBLEDevice::setSecurityAuth(true, false, true);           // bonding, no MITM, SC
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);  // Just Works

    // Configure the scanner here, not in the task: startScan() can be called
    // before the task first runs, and a scan started without callbacks reports
    // nothing at all.
    NimBLEScan* scan = NimBLEDevice::getScan();
    scan->setScanCallbacks(&s_scanCb, true);  // duplicates ON: keeps RSSI live
    // PASSIVE on purpose. With an active scan NimBLE holds a scannable device
    // back in an internal waiting list until its scan response arrives — if
    // that response never gets through, the device is never reported at all.
    // Passive reports every advertisement immediately. We lose names that only
    // live in the scan response, but pairWith() reads the real name off the
    // device after connecting anyway.
    scan->setActiveScan(false);
    scan->setMaxResults(0);                   // callbacks only: never accumulate
    scan->setInterval(100);                   // window == interval: listen
    scan->setWindow(100);                     // continuously, miss nothing

    loadPairing();

    return xTaskCreatePinnedToCore(task, "kbd", 8192, nullptr, 2, &s_task, 1) == pdPASS;
}

bool connected() { return s_connected; }

// Serial console as a keyboard: terminals send plain ASCII, plus ANSI escape
// sequences for the arrows (ESC [ A..D). Ctrl+C already *is* 0x03, which is
// our RUN/STOP code, so it needs no translation.
void drainSerial() {
    static int esc = 0;   // 0 = normal, 1 = saw ESC, 2 = saw ESC '['
    while (Serial.available()) {
        const int c = Serial.read();
        if (c < 0) break;

        if (esc == 1) { esc = (c == '[') ? 2 : 0; continue; }
        if (esc == 2) {
            esc = 0;
            switch (c) {
            case 'A': ringPush(0x91); break;   // up
            case 'B': ringPush(0x11); break;   // down
            case 'C': ringPush(0x1D); break;   // right
            case 'D': ringPush(0x9D); break;   // left
            default: break;
            }
            continue;
        }

        if (c == 0x1B) { esc = 1; continue; }              // start of a sequence
        if (c == '\r' || c == '\n') { ringPush(0x0D); continue; }
        if (c == 0x08 || c == 0x7F) { ringPush(0x14); continue; }
        if (c == 0x03) { ringPush(0x03); continue; }       // Ctrl+C -> RUN/STOP
        if (c >= 0x20 && c < 0x7F) ringPush(uint8_t(c));
    }
}

bool next(uint8_t& out) {
    if (!s_lock) return false;
    drainSerial();
    bool got = false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_tail != s_head) { out = s_ring[s_tail]; s_tail = (s_tail + 1) % RING; got = true; }
    xSemaphoreGive(s_lock);
    return got;
}

void startScan() {
    if (!s_task) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_deviceCount = 0;
    s_seenTotal   = 0;
    std::memset(s_devices, 0, sizeof s_devices);
    xSemaphoreGive(s_lock);
    NimBLEDevice::getScan()->clearResults();   // drop the controller's dup cache

    if (s_client && s_client->isConnected()) s_client->disconnect();
    s_pairState  = PairState::Idle;
    s_scanActive = true;
    const bool ok = NimBLEDevice::getScan()->start(0, false);   // 0 = until stopped
    if (config::BLE_SCAN_LOG) Serial.printf("[ble] scan start -> %s\n", ok ? "running" : "FAILED");
}

void stopScan() {
    if (!s_task) return;
    NimBLEDevice::getScan()->stop();
    s_scanActive = false;
    if (config::BLE_SCAN_LOG)
        Serial.printf("[ble] scan stop, %d kept / %d seen\n", s_deviceCount, s_seenTotal);
}

bool scanning() { return s_scanActive; }

int seenCount() {
    if (!s_lock) return 0;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const int n = s_seenTotal;
    xSemaphoreGive(s_lock);
    return n;
}

int deviceCount() {
    if (!s_lock) return 0;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const int n = s_deviceCount;
    xSemaphoreGive(s_lock);
    return n;
}

Device device(int i) {
    Device d{};
    if (!s_lock) return d;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (i >= 0 && i < s_deviceCount) d = s_devices[i];
    xSemaphoreGive(s_lock);
    return d;
}

void pairWith(int i) {
    Device d = device(i);
    if (!d.addr[0]) return;

    stopScan();

    // Keep the address *type* the scan reported — reconnecting with the wrong
    // one silently never finds the device again.
    s_candAddr = NimBLEAddress(std::string(d.addr), d.type);
    s_candType = d.type;
    std::snprintf(s_candAddrStr, sizeof s_candAddrStr, "%s", d.addr);
    std::snprintf(s_candName, sizeof s_candName, "%s", d.name);

    s_pairState = PairState::Connecting;
    s_probe = true;      // the task connects and verifies, then persists
}

PairState pairState() { return s_pairState; }

const char* savedName() { return s_haveSaved ? s_savedName : ""; }
const char* savedAddr() { return s_haveSaved ? s_savedAddrStr : ""; }

void forget() {
    s_haveSaved = false;
    s_savedName[0] = 0;
    s_savedAddrStr[0] = 0;
    s_pairState = PairState::Idle;
    if (fs::FS* fs = store()) fs->remove(storePath());
    if (s_client && s_client->isConnected()) s_client->disconnect();
}

} // namespace core::keyboard
