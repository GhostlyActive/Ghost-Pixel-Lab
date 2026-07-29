#include "ship.h"
#include "bodies.h"

#include <cmath>

namespace apps::outer {

namespace {

constexpr float THRUST     = 70.0f;
constexpr float YAWRATE    = 1.6f;
constexpr float PITCHRATE  = 1.4f;
constexpr float ROLLRATE   = 1.8f;
constexpr float DRAG       = 0.15f;
constexpr float BRAKE_DRAG = 2.2f;      // full brake: ~95% of the speed gone in 1.4 s
constexpr float GRAV       = 1.0f;
constexpr float LAND_SPEED = 20.0f;     // touchdown limit, relative to the planet
constexpr float LAUNCH_V   = 26.0f;

// Rotate two axis vectors of the body frame against each other. Yaw turns
// forward toward right, pitch turns forward toward up, roll turns right toward
// up — one helper covers all three.
void rotPair(float* a, float* b, float ang) {
    const float c = cosf(ang), s = sinf(ang);
    for (int i = 0; i < 3; ++i) {
        const float na = a[i] * c + b[i] * s;
        b[i] = -a[i] * s + b[i] * c;
        a[i] = na;
    }
}
void cross3(const float* a, const float* b, float* o) {
    o[0] = a[1]*b[2] - a[2]*b[1];
    o[1] = a[2]*b[0] - a[0]*b[2];
    o[2] = a[0]*b[1] - a[1]*b[0];
}
void norm3(float* a) {
    const float l = 1.0f / sqrtf(a[0]*a[0] + a[1]*a[1] + a[2]*a[2] + 1e-9f);
    a[0] *= l; a[1] *= l; a[2] *= l;
}

} // namespace

void Ship::reset() {
    x = 120; y = 40; z = -200;
    vx = vy = vz = 0;
    fwd[0]   = 0; fwd[1]   = 0; fwd[2]   = 1;
    right[0] = 1; right[1] = 0; right[2] = 0;
    up[0]    = 0; up[1]    = 1; up[2]    = 0;
    landedOn = -1;
    target   = -1;
    landX_ = landY_ = landZ_ = 0;
}

float Ship::speed() const { return sqrtf(vx*vx + vy*vy + vz*vz); }

void Ship::placeAbove(int body) {
    const Body& b = BODIES[body];
    x = b.x; y = b.y + b.radius + 70; z = b.z;   // pop out above the planet
    vx = 0; vy = 35; vz = 0;
    fwd[0]   =  0; fwd[1]   = 1; fwd[2]   = 0;
    right[0] = -1; right[1] = 0; right[2] = 0;
    up[0]    =  0; up[1]    = 0; up[2]    = 1;
    target   = body;
    landedOn = -1;
}

// A ray from the ship along its forward axis. Anything actually hit wins;
// otherwise the closest body inside a ~10 degree cone, so distant specks can
// still be targeted without pixel-perfect aim.
int Ship::bodyUnderCrosshair() const {
    const float fx = fwd[0], fy = fwd[1], fz = fwd[2];

    int best = -1; float bestT = 1e9f;
    for (int i = 0; i < COUNT; ++i) {
        const float ox = x - BODIES[i].x, oy = y - BODIES[i].y, oz = z - BODIES[i].z;
        const float b = ox*fx + oy*fy + oz*fz;
        const float c = ox*ox + oy*oy + oz*oz - BODIES[i].radius * BODIES[i].radius;
        const float disc = b*b - c;
        if (disc < 0) continue;
        const float tHit = -b - sqrtf(disc);
        if (tHit > 0 && tHit < bestT) { bestT = tHit; best = i; }
    }
    if (best >= 0) return best;

    float bestDot = 0.985f;   // ~10 deg cone
    for (int i = 0; i < COUNT; ++i) {
        float dx = BODIES[i].x - x, dy = BODIES[i].y - y, dz = BODIES[i].z - z;
        const float l = 1.0f / sqrtf(dx*dx + dy*dy + dz*dz + 1e-3f);
        const float d = (dx*l)*fx + (dy*l)*fy + (dz*l)*fz;
        if (d > bestDot) { bestDot = d; best = i; }
    }
    return best;
}

// Gravity from every body at once, plus thrust along the forward axis. The
// drag term is not physical — it just keeps the ship from accumulating speed
// forever, which makes the system flyable by hand.
void Ship::integrate(float thrust, float brake, float dt) {
    const float fx = fwd[0], fy = fwd[1], fz = fwd[2];
    float ax = fx * thrust * THRUST, ay = fy * thrust * THRUST, az = fz * thrust * THRUST;

    for (int i = 0; i < COUNT; ++i) {
        const float dx = BODIES[i].x - x, dy = BODIES[i].y - y, dz = BODIES[i].z - z;
        const float d2 = dx*dx + dy*dy + dz*dz + 1.0f;
        const float inv = 1.0f / (d2 * sqrtf(d2));
        const float g = GRAV * BODIES[i].gm * inv;
        ax += g * dx; ay += g * dy; az += g * dz;
    }

    vx += ax * dt; vy += ay * dt; vz += az * dt;
    float damp = 1.0f - (DRAG + BRAKE_DRAG * brake) * dt;
    if (damp < 0) damp = 0;              // a long frame must not reverse the velocity
    vx *= damp; vy *= damp; vz *= damp;
    x += vx * dt; y += vy * dt; z += vz * dt;
}

// Touching a solid body: slow enough relative to it and we land, otherwise we
// bounce off with half the rebound. Speed is always measured against the body,
// never against the origin, so landing on a fast-moving moon works.
void Ship::resolveContact(float t) {
    for (int i = 0; i < COUNT; ++i) {
        const Body& b = BODIES[i];
        if (b.sun || b.comet) continue;            // no solid surface to land on
        float dx = x - b.x, dy = y - b.y, dz = z - b.z;
        const float d = sqrtf(dx*dx + dy*dy + dz*dz);
        if (d >= b.radius + 0.5f) continue;

        const float inv = 1.0f / (d + 1e-3f);
        const float nx = dx*inv, ny = dy*inv, nz = dz*inv;
        x = b.x + nx * (b.radius + 0.5f);
        y = b.y + ny * (b.radius + 0.5f);
        z = b.z + nz * (b.radius + 0.5f);

        float pvx, pvy, pvz; bodyVelocity(i, t, pvx, pvy, pvz);
        const float rvx = vx - pvx, rvy = vy - pvy, rvz = vz - pvz;
        if (sqrtf(rvx*rvx + rvy*rvy + rvz*rvz) < LAND_SPEED) {
            landedOn = i;
            landX_ = x - b.x; landY_ = y - b.y; landZ_ = z - b.z;
            vx = vy = vz = 0;
        } else {
            const float vn = rvx*nx + rvy*ny + rvz*nz;
            vx = pvx + (rvx - 1.5f * vn * nx) * 0.5f;
            vy = pvy + (rvy - 1.5f * vn * ny) * 0.5f;
            vz = pvz + (rvz - 1.5f * vn * nz) * 0.5f;
        }
        return;
    }
}

void Ship::step(const Controls& c, float dt, float t) {
    // Body-relative rotations, then re-orthonormalise: forward is trusted,
    // right is rebuilt from up x forward, and up from forward x right.
    rotPair(fwd, right, c.yaw * YAWRATE * dt);
    rotPair(fwd, up,   -c.pitch * PITCHRATE * dt);
    rotPair(right, up,  c.roll * ROLLRATE * dt);
    norm3(fwd);
    cross3(up, fwd, right); norm3(right);
    cross3(fwd, right, up);

    if (landedOn >= 0) {
        // Stuck to the surface: ride the planet, and let A push us off it.
        const Body& b = BODIES[landedOn];
        x = b.x + landX_; y = b.y + landY_; z = b.z + landZ_;
        if (c.pick) {
            const float nl = 1.0f / sqrtf(landX_*landX_ + landY_*landY_ + landZ_*landZ_ + 1e-3f);
            float pvx, pvy, pvz; bodyVelocity(landedOn, t, pvx, pvy, pvz);
            vx = pvx + landX_ * nl * LAUNCH_V;
            vy = pvy + landY_ * nl * LAUNCH_V;
            vz = pvz + landZ_ * nl * LAUNCH_V;
            landedOn = -1;
        }
        return;
    }

    if (c.pick) {
        const int hit = bodyUnderCrosshair();
        if (hit >= 0) target = hit;
    }

    integrate(c.thrust, c.brake, dt);
    resolveContact(t);
}

int Ship::nearestLandable(float& altitude) const {
    int best = -1; float bestAlt = 1e9f;
    for (int i = 0; i < COUNT; ++i) {
        // Comets and the sun cannot be landed on, and the smallest moons are
        // too small to carry a terrain tile convincingly.
        if (BODIES[i].sun || BODIES[i].comet || BODIES[i].radius < 8.0f) continue;
        const float dx = BODIES[i].x - x, dy = BODIES[i].y - y, dz = BODIES[i].z - z;
        const float a = sqrtf(dx*dx + dy*dy + dz*dz) - BODIES[i].radius;
        if (a < bestAlt) { bestAlt = a; best = i; }
    }
    altitude = bestAlt;
    return best;
}

} // namespace apps::outer
