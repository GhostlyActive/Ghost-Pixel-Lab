// Composition root: bring up the board, register the apps, hand control to
// the app manager. Experiments live in src/apps/, hardware in src/board/,
// the framework glue in src/core/.
//
// To add a new app: subclass core::App (see src/apps/cube3d.h), create an
// instance below and core::manager::add() it. It shows up in the menu.
#include <Arduino.h>
#include <Wire.h>

#include "board/pins.h"
#include "board/display.h"
#include "board/expander.h"
#include "board/imu.h"
#include "board/touch.h"
#include "board/power.h"
#include "board/rtc.h"
#include "board/storage.h"

#include "core/app_manager.h"
#include "core/files.h"
#include "core/hw.h"

#include "apps/cube3d.h"
#include "apps/sensor_lab.h"
#include "apps/echo.h"
#include "apps/piano.h"
#include "apps/sand.h"
#include "apps/maze.h"
#include "apps/level.h"
#include "apps/music.h"
#include "apps/wifi_scan.h"
#include "apps/pad_lab.h"
#include "apps/outer_pixels.h"
#include "apps/ghost_basic.h"
#include "apps/ble_scan.h"
#include "apps/disk.h"

namespace core::hw {
bool imu = false, touch = false, power = false, rtc = false;
bool flashFs = false, sd = false;
}

namespace {
apps::Cube3D        cubeApp;
apps::SensorLab     sensorLabApp;
apps::Echo          echoApp;
apps::Piano         pianoApp;
apps::Sand          sandApp;
apps::MazeBall      mazeApp;
apps::Level         levelApp;
apps::Music         musicApp;
apps::WifiScan      wifiScanApp;
apps::PadLab        padLabApp;
apps::Outer_Pixels  outerPixelsApp;
apps::GhostBasic           ghostBasicApp;
apps::BleScan       bleScanApp;
apps::Disk          diskApp;
}

void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.println("\n[boot] Ghost Pixel Lab");

    if (!board::display::begin()) {
        Serial.println("[boot] display init failed");
        while (true) delay(1000);
    }

    Wire.begin(board::pins::I2C_SDA, board::pins::I2C_SCL, 400000);
    board::expander::begin();

    core::hw::imu     = board::imu::begin();
    core::hw::touch   = board::touch::begin();
    core::hw::power   = board::power::begin();
    core::hw::rtc     = board::rtc::begin();
    core::hw::flashFs = board::storage::beginFlash();
    core::hw::sd      = board::storage::beginSD();
    core::files::begin();   // /GHOST layout on SD, or flash as fallback
    Serial.printf("[boot] imu=%d touch=%d pmu=%d rtc=%d fs=%d sd=%d\n",
                  core::hw::imu, core::hw::touch, core::hw::power,
                  core::hw::rtc, core::hw::flashFs, core::hw::sd);

    core::manager::add(cubeApp);
    core::manager::add(sensorLabApp);
    core::manager::add(echoApp);
    core::manager::add(pianoApp);
    core::manager::add(sandApp);
    core::manager::add(mazeApp);
    core::manager::add(levelApp);
    core::manager::add(musicApp);
    core::manager::add(wifiScanApp);
    core::manager::add(padLabApp);
    core::manager::add(outerPixelsApp);
    core::manager::add(ghostBasicApp);
    core::manager::add(bleScanApp);
    core::manager::add(diskApp);
    core::manager::begin();
}

void loop() {
    core::manager::tick();
}
