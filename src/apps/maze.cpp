#include "maze.h"
#include "tilt.h"
#include "board/display.h"

#include <Arduino.h>
#include <cmath>
#include <cstdio>

namespace apps {

namespace {

using board::gfx::Surface;

constexpr int   CELL    = 52;
constexpr int   OX      = 2;            // maze origin: 7*52=364, 8*52=416
constexpr int   OY      = 26;
constexpr int   WALL    = 6;            // wall thickness
constexpr float BALL_R  = 13.0f;
constexpr float ACCEL   = 1100.0f;      // px/s^2 per g of tilt
constexpr float DAMPING = 1.4f;         // per second
constexpr float BOUNCE  = 0.25f;

constexpr uint16_t COL_WALL   = 0x07FF;
constexpr uint16_t COL_BALL   = 0xFFFF;
constexpr uint16_t COL_GOAL   = 0x07E0;
constexpr uint16_t COL_DIM    = 0x8410;

struct Rect { int x, y, w, h; };

// Push a circle out of a rect; kills the velocity component into the wall.
void resolveCircleRect(float& px, float& py, float& vx, float& vy, const Rect& r) {
    const float cx = fminf(fmaxf(px, float(r.x)), float(r.x + r.w));
    const float cy = fminf(fmaxf(py, float(r.y)), float(r.y + r.h));
    const float dx = px - cx, dy = py - cy;
    const float d2 = dx * dx + dy * dy;
    if (d2 >= BALL_R * BALL_R) return;

    if (d2 > 0.0001f) {
        const float d = sqrtf(d2);
        const float nx = dx / d, ny = dy / d;
        const float push = BALL_R - d;
        px += nx * push;
        py += ny * push;
        const float vn = vx * nx + vy * ny;
        if (vn < 0) {
            vx -= (1.0f + BOUNCE) * vn * nx;
            vy -= (1.0f + BOUNCE) * vn * ny;
        }
    } else {
        // Center inside the rect: push out along the shortest axis.
        const float left   = px - r.x, right = r.x + r.w - px;
        const float top    = py - r.y, bot   = r.y + r.h - py;
        const float m = fminf(fminf(left, right), fminf(top, bot));
        if      (m == left)  { px = r.x - BALL_R;        vx = 0; }
        else if (m == right) { px = r.x + r.w + BALL_R;  vx = 0; }
        else if (m == top)   { py = r.y - BALL_R;        vy = 0; }
        else                 { py = r.y + r.h + BALL_R;  vy = 0; }
    }
}

} // namespace

void MazeBall::onEnter() {
    rng_ ^= millis();
    generate();
    resetBall();
    level_    = 1;
    winTimer_ = 0;
}

void MazeBall::resetBall() {
    px_ = OX + CELL / 2.0f;
    py_ = OY + CELL / 2.0f;
    vx_ = vy_ = 0;
}

// Depth-first backtracker.
void MazeBall::generate() {
    for (auto& row : walls_)
        for (auto& w : row) w = 0x03;

    bool visited[ROWS][COLS] = {};
    int  stack[ROWS * COLS];
    int  sp  = 0;
    int  cur = 0;
    visited[0][0] = true;

    while (true) {
        const int cx = cur % COLS, cy = cur / COLS;
        int nbs[4], nn = 0;
        if (cx > 0        && !visited[cy][cx - 1]) nbs[nn++] = cur - 1;
        if (cx < COLS - 1 && !visited[cy][cx + 1]) nbs[nn++] = cur + 1;
        if (cy > 0        && !visited[cy - 1][cx]) nbs[nn++] = cur - COLS;
        if (cy < ROWS - 1 && !visited[cy + 1][cx]) nbs[nn++] = cur + COLS;

        if (nn > 0) {
            const int nxt = nbs[rnd() % nn];
            const int nx = nxt % COLS, ny = nxt / COLS;
            if      (nx > cx) walls_[cy][cx] &= ~0x01;   // knock right
            else if (nx < cx) walls_[ny][nx] &= ~0x01;
            else if (ny > cy) walls_[cy][cx] &= ~0x02;   // knock bottom
            else              walls_[ny][nx] &= ~0x02;
            visited[ny][nx] = true;
            stack[sp++] = cur;
            cur = nxt;
        } else if (sp > 0) {
            cur = stack[--sp];
        } else {
            break;
        }
    }
}

void MazeBall::collide() {
    const int cx = static_cast<int>((px_ - OX) / CELL);
    const int cy = static_cast<int>((py_ - OY) / CELL);

    // Outer borders.
    const Rect borders[4] = {
        {OX - WALL,             OY - WALL,             COLS * CELL + 2 * WALL, WALL},
        {OX - WALL,             OY + ROWS * CELL,      COLS * CELL + 2 * WALL, WALL},
        {OX - WALL,             OY - WALL,             WALL, ROWS * CELL + 2 * WALL},
        {OX + COLS * CELL,      OY - WALL,             WALL, ROWS * CELL + 2 * WALL},
    };
    for (const auto& r : borders) resolveCircleRect(px_, py_, vx_, vy_, r);

    // Inner walls near the ball.
    for (int y = cy - 1; y <= cy + 1; ++y) {
        for (int x = cx - 1; x <= cx + 1; ++x) {
            if (x < 0 || x >= COLS || y < 0 || y >= ROWS) continue;
            if (walls_[y][x] & 0x01) {
                const Rect r{OX + (x + 1) * CELL - WALL / 2, OY + y * CELL - WALL / 2,
                             WALL, CELL + WALL};
                resolveCircleRect(px_, py_, vx_, vy_, r);
            }
            if (walls_[y][x] & 0x02) {
                const Rect r{OX + x * CELL - WALL / 2, OY + (y + 1) * CELL - WALL / 2,
                             CELL + WALL, WALL};
                resolveCircleRect(px_, py_, vx_, vy_, r);
            }
        }
    }
}

void MazeBall::update(const core::Input& in, float dt) {
    if (winTimer_ > 0) {
        winTimer_ -= dt;
        if (winTimer_ <= 0) {
            ++level_;
            generate();
            resetBall();
        }
        return;
    }

    float ax = 0, ay = 0;
    float gx, gy;
    if (tilt::gravity(gx, gy)) {
        ax = gx * ACCEL;
        ay = gy * ACCEL;
    } else if (in.pressed) {
        // No IMU: drag relative to the ball pushes it.
        ax = (in.x - px_) * 4.0f;
        ay = (in.y - py_) * 4.0f;
    }

    // Two substeps keep the collision stable at low frame rates.
    for (int step = 0; step < 2; ++step) {
        const float h = dt * 0.5f;
        vx_ += ax * h;
        vy_ += ay * h;
        const float damp = 1.0f - DAMPING * h;
        vx_ *= damp;
        vy_ *= damp;
        px_ += vx_ * h;
        py_ += vy_ * h;
        collide();
    }

    // Goal: bottom-right cell.
    const float goalX = OX + (COLS - 1) * CELL + CELL / 2.0f;
    const float goalY = OY + (ROWS - 1) * CELL + CELL / 2.0f;
    const float dx = px_ - goalX, dy = py_ - goalY;
    if (dx * dx + dy * dy < 20.0f * 20.0f) winTimer_ = 1.2f;
}

void MazeBall::render(Surface& s) {
    const int W = board::display::WIDTH;
    char line[24];

    s.clear(0x0000);

    snprintf(line, sizeof(line), "MAZE %d", level_);
    s.text((W - s.textWidth(line, 2)) / 2, 5, line, COL_DIM, 2);

    // Borders.
    s.fillRect(OX - WALL, OY - WALL, COLS * CELL + 2 * WALL, WALL, COL_WALL);
    s.fillRect(OX - WALL, OY + ROWS * CELL, COLS * CELL + 2 * WALL, WALL, COL_WALL);
    s.fillRect(OX - WALL, OY - WALL, WALL, ROWS * CELL + 2 * WALL, COL_WALL);
    s.fillRect(OX + COLS * CELL, OY - WALL, WALL, ROWS * CELL + 2 * WALL, COL_WALL);

    // Inner walls.
    for (int y = 0; y < ROWS; ++y) {
        for (int x = 0; x < COLS; ++x) {
            if (walls_[y][x] & 0x01) {
                s.fillRect(OX + (x + 1) * CELL - WALL / 2, OY + y * CELL - WALL / 2,
                           WALL, CELL + WALL, COL_WALL);
            }
            if (walls_[y][x] & 0x02) {
                s.fillRect(OX + x * CELL - WALL / 2, OY + (y + 1) * CELL - WALL / 2,
                           CELL + WALL, WALL, COL_WALL);
            }
        }
    }

    // Goal pad.
    const int goalX = OX + (COLS - 1) * CELL + CELL / 2;
    const int goalY = OY + (ROWS - 1) * CELL + CELL / 2;
    s.filledCircle(goalX, goalY, 16, COL_GOAL);
    s.filledCircle(goalX, goalY, 9, 0x0000);

    // Ball.
    s.filledCircle(static_cast<int>(px_), static_cast<int>(py_),
                   static_cast<int>(BALL_R), COL_BALL);
    s.filledCircle(static_cast<int>(px_), static_cast<int>(py_), 5, 0x051F);

    if (winTimer_ > 0) {
        s.fillRect(0, 190, W, 68, 0x07E0);
        s.text((W - s.textWidth("GOAL!", 4)) / 2, 208, "GOAL!", 0x0000, 4);
    }
}

} // namespace apps
