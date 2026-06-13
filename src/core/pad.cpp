#include "pad.h"
#include "config.h"

#include <Arduino.h>
#include <XboxSeriesXControllerESP32_asukiaaa.hpp>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <cmath>

namespace core::pad {

namespace {

XboxSeriesXControllerESP32_asukiaaa::Core* s_ctl = nullptr;
TaskHandle_t      s_task = nullptr;
SemaphoreHandle_t s_lock = nullptr;
State             s_state;

bool s_rumblePending = false;
XboxSeriesXHIDReportBuilder_asukiaaa::ReportBase s_rumbleRepo;

// Raw 0..65535 -> -1..1 with deadzone, rescaled so motion starts at 0.
float axis(uint16_t raw) {
    float v = (float(raw) - 32767.5f) / 32767.5f;
    const float dz = config::PAD_DEADZONE;
    if (fabsf(v) < dz) return 0.0f;
    const float sign = v < 0 ? -1.0f : 1.0f;
    v = (fabsf(v) - dz) / (1.0f - dz);
    return sign * fminf(v, 1.0f);
}

void padTask(void*) {
    for (;;) {
        // Scans and (re)connects; blocks for a while during a connect, which
        // is exactly why this runs on its own task and not the frame loop.
        s_ctl->onLoop();

        State st;
        const auto& n = s_ctl->xboxNotif;
        st.connected = s_ctl->isConnected() && !s_ctl->isWaitingForFirstNotification();
        if (st.connected) {
            st.lx = axis(n.joyLHori);
            st.ly = axis(n.joyLVert);
            st.rx = axis(n.joyRHori);
            st.ry = axis(n.joyRVert);
            st.lt = n.trigLT / 1023.0f;
            st.rt = n.trigRT / 1023.0f;
            st.a  = n.btnA;  st.b  = n.btnB;  st.x  = n.btnX;  st.y  = n.btnY;
            st.lb = n.btnLB; st.rb = n.btnRB; st.ls = n.btnLS; st.rs = n.btnRS;
            st.up   = n.btnDirUp;   st.down  = n.btnDirDown;
            st.left = n.btnDirLeft; st.right = n.btnDirRight;
            st.menu = n.btnStart;   st.view  = n.btnSelect;
            st.xbox = n.btnXbox;    st.share = n.btnShare;
            st.battery = s_ctl->battery;
        }

        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_state = st;
        const bool doRumble = s_rumblePending && st.connected;
        const auto repo     = s_rumbleRepo;
        s_rumblePending     = false;
        xSemaphoreGive(s_lock);

        // BLE writes happen on this task only; apps just queue the request.
        if (doRumble) s_ctl->writeHIDReport(repo);

        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

} // namespace

bool begin() {
    if (s_task) return true;
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) return false;
    s_ctl = new XboxSeriesXControllerESP32_asukiaaa::Core();
    s_ctl->begin();
    return xTaskCreatePinnedToCore(padTask, "pad", 8192, nullptr, 3,
                                   &s_task, 0) == pdPASS;
}

bool connected() {
    return state().connected;
}

State state() {
    State st;
    if (!s_task) return st;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    st = s_state;
    xSemaphoreGive(s_lock);
    return st;
}

void rumble(uint8_t body, uint8_t triggers, uint16_t durationMs) {
    if (!s_task) return;
    XboxSeriesXHIDReportBuilder_asukiaaa::ReportBase repo;
    repo.setAllOff();
    if (body > 100)     body = 100;
    if (triggers > 100) triggers = 100;
    repo.v.select.shake  = body > 0;
    repo.v.select.center = body > 0;
    repo.v.select.left   = triggers > 0;
    repo.v.select.right  = triggers > 0;
    repo.v.power.shake   = body;
    repo.v.power.center  = body;
    repo.v.power.left    = triggers;
    repo.v.power.right   = triggers;
    const uint16_t t = durationMs / 10;  // unit: 10 ms
    repo.v.timeActive = t > 255 ? 255 : static_cast<uint8_t>(t);

    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_rumbleRepo    = repo;
    s_rumblePending = true;
    xSemaphoreGive(s_lock);
}

} // namespace core::pad
