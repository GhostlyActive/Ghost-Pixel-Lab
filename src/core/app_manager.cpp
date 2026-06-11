#include "app_manager.h"
#include "hw.h"
#include "menu.h"

#include "board/display.h"
#include "board/touch.h"

#include <Arduino.h>

namespace core::manager {

namespace {

constexpr int BOOT_BTN_PIN  = 0;   // BOOT button, active low
constexpr int EDGE_ZONE_PX  = 40;  // swipe must start this close to the top
constexpr int SWIPE_DIST_PX = 90;  // ... and travel this far down

App* s_apps[MAX_APPS];
int  s_count   = 0;
App* s_current = nullptr;

Input s_input;
bool  s_swipeArmed  = false;
bool  s_bootWasDown = false;

uint32_t s_lastUs      = 0;
uint32_t s_frames      = 0;
uint32_t s_fpsWindowMs = 0;
float    s_fps         = 0.0f;

uint32_t   s_accUpdateUs = 0, s_accDrawUs = 0, s_accShowUs = 0;
FrameStats s_stats{};
uint32_t   s_lastLogMs = 0;

void sampleInput() {
    s_input.justPressed  = false;
    s_input.justReleased = false;
    if (!hw::touch) {
        s_input.pressed = false;
        return;
    }

    const auto p = board::touch::read();
    if (p.pressed) {
        if (!s_input.pressed) {
            s_input.justPressed = true;
            s_input.startX = p.x;
            s_input.startY = p.y;
        }
        s_input.pressed = true;
        s_input.x = p.x;
        s_input.y = p.y;
    } else if (s_input.pressed) {
        s_input.justReleased = true;
        s_input.pressed = false;
    }
}

// Top-edge swipe-down or a BOOT-button press returns to the menu.
void pollHomeControls() {
    if (s_current == &menu::instance()) return;

    if (s_input.justPressed) s_swipeArmed = s_input.startY < EDGE_ZONE_PX;
    if (!s_input.pressed) s_swipeArmed = false;
    if (s_swipeArmed && s_input.y - s_input.startY > SWIPE_DIST_PX) {
        s_swipeArmed = false;
        goHome();
        return;
    }

    const bool down = digitalRead(BOOT_BTN_PIN) == LOW;
    if (s_bootWasDown && !down) goHome();
    s_bootWasDown = down;
}

} // namespace

void add(App& app) {
    if (s_count < MAX_APPS) s_apps[s_count++] = &app;
}

int  count()   { return s_count; }
App& at(int i) { return *s_apps[i]; }

void begin() {
    pinMode(BOOT_BTN_PIN, INPUT_PULLUP);
    s_current = &menu::instance();
    s_current->onEnter();
    s_lastUs      = micros();
    s_fpsWindowMs = millis();
}

void launch(App& app) {
    if (s_current == &app) return;
    if (s_current) s_current->onExit();
    s_current = &app;
    s_current->onEnter();
}

void goHome() { launch(menu::instance()); }

float      fps()        { return s_fps; }
FrameStats frameStats() { return s_stats; }

void tick() {
    const uint32_t nowUs = micros();
    float dt = (nowUs - s_lastUs) * 1e-6f;
    s_lastUs = nowUs;
    if (dt < 0.0f || dt > 0.1f) dt = 0.0f;  // first frame / long stall

    sampleInput();
    pollHomeControls();

    // Pin the app for this frame so a launch() inside update() doesn't
    // render a half-initialised successor.
    App* app = s_current;
    const uint32_t t0 = micros();
    app->update(s_input, dt);

    const uint32_t t1 = micros();
    auto s = board::display::canvas();
    app->render(s);

    const uint32_t t2 = micros();
    board::display::present();
    const uint32_t t3 = micros();

    s_accUpdateUs += t1 - t0;
    s_accDrawUs   += t2 - t1;
    s_accShowUs   += t3 - t2;

    ++s_frames;
    const uint32_t nowMs = millis();
    const uint32_t win   = nowMs - s_fpsWindowMs;
    if (win >= 500) {
        const float n = static_cast<float>(s_frames);
        s_fps            = n * 1000.0f / static_cast<float>(win);
        s_stats.updateMs = s_accUpdateUs / n / 1000.0f;
        s_stats.drawMs   = s_accDrawUs   / n / 1000.0f;
        s_stats.showMs   = s_accShowUs   / n / 1000.0f;
        s_frames = 0;
        s_accUpdateUs = s_accDrawUs = s_accShowUs = 0;
        s_fpsWindowMs = nowMs;

        if (nowMs - s_lastLogMs >= 2000) {
            s_lastLogMs = nowMs;
            Serial.printf("fps=%.1f update=%.1fms draw=%.1fms show=%.1fms\n",
                          s_fps, s_stats.updateMs, s_stats.drawMs, s_stats.showMs);
        }
    }
}

} // namespace core::manager
