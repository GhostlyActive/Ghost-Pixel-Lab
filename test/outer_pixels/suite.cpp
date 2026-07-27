// Outer Pixels on the host: orbital mechanics, the flight model, the terrain
// generator and both renderers, checked without a device.
//
// The point is not pixel-perfect rendering — it is that the simulation obeys
// the rules the app depends on (moons follow their planets, a slow touchdown
// lands and a fast one bounces, the terrain tile wraps) and that every piece
// still compiles and draws after the code moves.
#include "apps/outer_pixels/bodies.h"
#include "apps/outer_pixels/ship.h"
#include "apps/outer_pixels/terrain.h"
#include "apps/outer_pixels/space_view.h"
#include "apps/outer_pixels/surface_view.h"
#include "apps/outer_pixels/clouds.h"
#include "apps/outer_pixels/colors.h"
#include "board/display.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace apps::outer;

static int passN = 0, failN = 0;

static void check(const char* name, bool ok, const std::string& detail = "") {
    if (ok) { ++passN; return; }
    ++failN;
    std::printf("FAIL  %s%s%s\n", name, detail.empty() ? "" : "  ", detail.c_str());
}

static float dist(const Body& a, const Body& b) {
    const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return std::sqrt(dx*dx + dy*dy + dz*dz);
}

// A frame buffer big enough for the real panel, so the renderers run at the
// dimensions they will actually see.
static std::vector<uint16_t> g_buf(board::display::WIDTH * board::display::HEIGHT);
static board::gfx::Surface g_surf{g_buf.data(), board::display::WIDTH, board::display::HEIGHT};

static int nonBackgroundPixels(uint16_t bg) {
    int n = 0;
    for (uint16_t p : g_buf) if (p != bg) ++n;
    return n;
}

int main() {
    // --- orbits -------------------------------------------------------------
    {
        update(0.0f);
        check("the sun sits at the origin",
              BODIES[SUN].x == 0 && BODIES[SUN].y == 0 && BODIES[SUN].z == 0);

        // Every body must list its parent before itself, or the single forward
        // pass in update() would place children off last frame's parent.
        bool ordered = true;
        for (int i = 0; i < COUNT; ++i)
            if (BODIES[i].parent >= i) ordered = false;
        check("parents come before their children in the table", ordered);

        // A planet stays at its orbital radius from the sun, whenever we look.
        bool radiusHeld = true;
        std::string detail;
        for (float t = 0; t < 60.0f; t += 3.7f) {
            update(t);
            for (int i = 0; i < COUNT; ++i) {
                if (BODIES[i].parent != SUN) continue;
                const float r = dist(BODIES[i], BODIES[SUN]);
                if (std::fabs(r - BODIES[i].orbR) > 0.5f) {
                    radiusHeld = false;
                    detail = std::string(BODIES[i].name) + " r=" + std::to_string(r);
                }
            }
        }
        check("planets hold their orbital radius", radiusHeld, detail);

        // A moon orbits its planet, not the sun: the distance to its parent is
        // what stays constant while the pair travels.
        bool moonsHeld = true;
        for (float t = 0; t < 40.0f; t += 2.3f) {
            update(t);
            for (int i = 0; i < COUNT; ++i) {
                const int p = BODIES[i].parent;
                if (p <= SUN) continue;                  // only moons have a planet parent
                if (std::fabs(dist(BODIES[i], BODIES[p]) - BODIES[i].orbR) > 0.5f)
                    moonsHeld = false;
            }
        }
        check("moons follow their planet", moonsHeld);

        // Bodies actually move, and the whole system is deterministic in t —
        // stepping straight to a time gives the same answer as arriving there.
        update(0.0f);
        const float x0 = BODIES[1].x;
        update(5.0f);
        const float x5 = BODIES[1].x;
        update(0.0f); update(5.0f);
        check("orbits move at all", std::fabs(x5 - x0) > 1.0f);
        check("position depends only on t", BODIES[1].x == x5);

        // bodyVelocity is the derivative of the orbit; compare it against a
        // finite difference of the positions it is supposed to describe.
        const float t = 7.0f, h = 0.001f;
        update(t - h);
        const float bx = BODIES[2].x, by = BODIES[2].y, bz = BODIES[2].z;
        update(t + h);
        const float fdx = (BODIES[2].x - bx) / (2 * h);
        const float fdy = (BODIES[2].y - by) / (2 * h);
        const float fdz = (BODIES[2].z - bz) / (2 * h);
        float vX, vY, vZ; bodyVelocity(2, t, vX, vY, vZ);
        const float err = std::sqrt((vX-fdx)*(vX-fdx) + (vY-fdy)*(vY-fdy) + (vZ-fdz)*(vZ-fdz));
        check("orbital velocity matches the moving position", err < 0.05f,
              "err=" + std::to_string(err));
    }

    // --- terrain ------------------------------------------------------------
    {
        static std::vector<uint8_t>  heights(Terrain::HEIGHT_BYTES);
        static std::vector<uint16_t> colors(Terrain::SIZE * Terrain::SIZE);
        Terrain terrain;

        check("an unattached tile reports so", !terrain.attached());
        terrain.attach(heights.data(), colors.data());
        check("nothing is held before generating", terrain.held() == -1);

        terrain.generate(2, BODIES[2].col);
        check("generate records which planet it built", terrain.held() == 2);

        // The tile has to have relief — a flat or single-valued map would mean
        // the noise collapsed.
        int lo = 999, hi = -1;
        for (int y = 0; y < Terrain::SIZE; ++y)
            for (int x = 0; x < Terrain::SIZE; ++x) {
                const int h = int(terrain.heightAt(x, y) / Terrain::HSCALE + 0.5f);
                if (h < lo) lo = h;
                if (h > hi) hi = h;
            }
        check("the terrain has relief", hi - lo > 60,
              "range " + std::to_string(lo) + ".." + std::to_string(hi));

        // Sampling wraps in both directions, which is what makes flying
        // endless — position 256 is position 0, and negatives wrap too.
        check("sampling wraps at the tile edge",
              terrain.heightAt(0, 0) == terrain.heightAt(Terrain::SIZE, Terrain::SIZE) &&
              terrain.heightAt(3, 5) == terrain.heightAt(3 - Terrain::SIZE, 5));

        // Two planets must not look alike: same generator, different seed.
        const float sample2 = terrain.heightAt(40, 40);
        terrain.generate(4, BODIES[4].col);
        check("a different planet builds a different tile",
              terrain.held() == 4 && terrain.heightAt(40, 40) != sample2);
    }

    // --- flight model -------------------------------------------------------
    {
        update(0.0f);
        Ship ship;
        ship.reset();
        check("a fresh ship is flying, not landed", ship.landedOn == -1 && ship.target == -1);

        // The body frame must stay orthonormal after arbitrary steering, or the
        // view would slowly shear.
        Controls c;
        c.yaw = 0.7f; c.pitch = -0.4f; c.roll = 0.9f;
        for (int i = 0; i < 400; ++i) ship.step(c, 0.016f, 0.0f);
        auto len = [](const float* v) { return std::sqrt(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]); };
        auto dot = [](const float* a, const float* b) {
            return a[0]*b[0] + a[1]*b[1] + a[2]*b[2]; };
        const bool unit = std::fabs(len(ship.fwd) - 1) < 1e-3f &&
                          std::fabs(len(ship.right) - 1) < 1e-3f &&
                          std::fabs(len(ship.up) - 1) < 1e-3f;
        const bool perp = std::fabs(dot(ship.fwd, ship.right)) < 1e-3f &&
                          std::fabs(dot(ship.fwd, ship.up))    < 1e-3f &&
                          std::fabs(dot(ship.right, ship.up))  < 1e-3f;
        check("steering keeps the body frame orthonormal", unit && perp);

        // Gravity pulls: dropped from rest near a planet, the ship falls toward
        // it rather than drifting away.
        update(0.0f);
        const Body& target = BODIES[2];
        ship.reset();
        ship.x = target.x + target.radius + 120; ship.y = target.y; ship.z = target.z;
        ship.vx = ship.vy = ship.vz = 0;
        const float before = std::sqrt((ship.x-target.x)*(ship.x-target.x) +
                                       (ship.y-target.y)*(ship.y-target.y) +
                                       (ship.z-target.z)*(ship.z-target.z));
        Controls drift;
        for (int i = 0; i < 60; ++i) ship.step(drift, 0.016f, 0.0f);
        const float after = std::sqrt((ship.x-target.x)*(ship.x-target.x) +
                                      (ship.y-target.y)*(ship.y-target.y) +
                                      (ship.z-target.z)*(ship.z-target.z));
        check("gravity pulls the ship toward a planet", after < before,
              std::to_string(before) + " -> " + std::to_string(after));

        // Touching a surface slowly lands. "Slowly" is relative to the planet,
        // not to the origin: Brontes sweeps its orbit at over 20 units/s, which
        // is already past the touchdown limit — so a ship hanging motionless in
        // absolute space is hit by the planet and bounces. Matching its
        // velocity is what makes it a landing.
        float pvx, pvy, pvz; bodyVelocity(2, 0.0f, pvx, pvy, pvz);
        ship.reset();
        ship.x = target.x + target.radius - 0.1f; ship.y = target.y; ship.z = target.z;
        ship.vx = pvx; ship.vy = pvy; ship.vz = pvz;
        ship.step(drift, 0.016f, 0.0f);
        check("a touchdown at matched speed lands", ship.landedOn == 2,
              "landedOn=" + std::to_string(ship.landedOn));

        // ...and A launches again, leaving the surface with outward speed.
        Controls press; press.pick = true;
        ship.step(press, 0.016f, 0.0f);
        check("A launches a landed ship", ship.landedOn == -1 && ship.speed() > 1.0f);

        // The same contact at speed must bounce instead of landing.
        ship.reset();
        ship.x = target.x + target.radius - 0.1f; ship.y = target.y; ship.z = target.z;
        ship.vx = -400.0f; ship.vy = ship.vz = 0;         // straight at the planet, fast
        ship.step(drift, 0.016f, 0.0f);
        check("a fast impact bounces instead of landing",
              ship.landedOn == -1 && ship.vx > 0,
              "landedOn=" + std::to_string(ship.landedOn) + " vx=" + std::to_string(ship.vx));

        // The nearest landable body skips the sun, the comets and the specks.
        update(0.0f);
        ship.reset();
        ship.x = target.x; ship.y = target.y + target.radius + 40; ship.z = target.z;
        float alt = -1;
        const int nearest = ship.nearestLandable(alt);
        check("nearestLandable finds the planet under the ship",
              nearest == 2 && alt > 30 && alt < 50,
              "body=" + std::to_string(nearest) + " alt=" + std::to_string(alt));
        bool skipsUnlandable = true;
        for (int i = 0; i < COUNT; ++i) {
            if (!(BODIES[i].sun || BODIES[i].comet)) continue;
            ship.x = BODIES[i].x; ship.y = BODIES[i].y + BODIES[i].radius + 1; ship.z = BODIES[i].z;
            float a2 = 0;
            if (ship.nearestLandable(a2) == i) skipsUnlandable = false;
        }
        check("the sun and comets are never landable", skipsUnlandable);

        // Picking: aim straight at a planet and A targets it.
        update(0.0f);
        ship.reset();
        ship.x = target.x; ship.y = target.y; ship.z = target.z - 300;
        const float l = 1.0f / 300.0f;
        ship.fwd[0] = 0; ship.fwd[1] = 0; ship.fwd[2] = 300 * l;
        ship.right[0] = 1; ship.right[1] = 0; ship.right[2] = 0;
        ship.up[0] = 0; ship.up[1] = 1; ship.up[2] = 0;
        ship.step(press, 0.001f, 0.0f);
        check("A targets the body under the crosshair", ship.target == 2,
              "target=" + std::to_string(ship.target));
    }

    // --- colour helpers -----------------------------------------------------
    {
        check("blend at the ends returns the ends",
              blend(0x1234, 0xABCD, 0.0f) == 0x1234 && blend(0x1234, 0xABCD, 1.0f) == 0xABCD);
        check("blend clamps outside 0..1",
              blend(0x1234, 0xABCD, -5.0f) == 0x1234 && blend(0x1234, 0xABCD, 9.0f) == 0xABCD);
        check("scaleRGB to zero is black and to one is unchanged",
              scaleRGB(0xFFFF, 0.0f) == 0x0000 && scaleRGB(0xF81F, 1.0f) == 0xF81F);
    }

    // --- renderers ----------------------------------------------------------
    {
        update(3.0f);
        Ship ship;
        ship.reset();
        // Put the sun in front of the ship so there is guaranteed to be
        // something to draw.
        ship.x = 0; ship.y = 0; ship.z = -400;
        ship.fwd[0] = 0; ship.fwd[1] = 0; ship.fwd[2] = 1;
        ship.right[0] = 1; ship.right[1] = 0; ship.right[2] = 0;
        ship.up[0] = 0; ship.up[1] = 1; ship.up[2] = 0;

        SpaceView space;
        space.reset();
        std::fill(g_buf.begin(), g_buf.end(), uint16_t(0xDEAD));
        space.render(g_surf, ship, false, true, -1.0f);
        check("the space view paints the frame", nonBackgroundPixels(0xDEAD) > 10000);

        // Orbit lines have to add ink, not replace the frame.
        std::fill(g_buf.begin(), g_buf.end(), uint16_t(0xDEAD));
        space.render(g_surf, ship, false, true, -1.0f);
        const int plain = nonBackgroundPixels(0x0000);
        std::fill(g_buf.begin(), g_buf.end(), uint16_t(0xDEAD));
        space.render(g_surf, ship, true, true, -1.0f);
        const int withOrbits = nonBackgroundPixels(0x0000);
        check("orbit lines draw more than without", withOrbits > plain,
              std::to_string(plain) + " -> " + std::to_string(withOrbits));

        // The cloud transition covers the screen at the start and has cleared
        // by the end of DIVE_TIME.
        std::fill(g_buf.begin(), g_buf.end(), uint16_t(0x0000));
        drawClouds(g_surf, 0x6D7F, 0.0f);
        const int early = nonBackgroundPixels(0x0000);
        std::fill(g_buf.begin(), g_buf.end(), uint16_t(0x0000));
        drawClouds(g_surf, 0x6D7F, DIVE_TIME);
        const int late = nonBackgroundPixels(0x0000);
        check("the cloud dive starts thick and thins out", early > late * 4,
              std::to_string(early) + " -> " + std::to_string(late));

        // The surface view: fly a while, then draw. The camera must stay above
        // the terrain and the frame must not be empty sky.
        static std::vector<uint8_t>  heights(Terrain::HEIGHT_BYTES);
        static std::vector<uint16_t> colors(Terrain::SIZE * Terrain::SIZE);
        Terrain terrain;
        terrain.attach(heights.data(), colors.data());
        terrain.generate(2, BODIES[2].col);

        SurfaceView view;
        view.enter();
        Controls fly;
        fly.yaw = 0.3f; fly.thrust = 0.5f;
        bool stayed = true, aboveGround = true;
        for (int i = 0; i < 120; ++i) {
            if (!view.update(fly, 0.016f, terrain)) stayed = false;
            if (view.altitude() < 0) aboveGround = false;
        }
        check("normal flight stays in surface mode", stayed);
        check("the camera never sinks below the ground", aboveGround);

        std::fill(g_buf.begin(), g_buf.end(), uint16_t(0xDEAD));
        view.render(g_surf, terrain, BODIES[2], true);
        check("the surface view paints the frame", nonBackgroundPixels(0xDEAD) > 10000);

        // Climbing hard must eventually hand control back to space mode.
        view.enter();
        Controls climb; climb.pitch = 1.0f;
        bool left = false;
        for (int i = 0; i < 600 && !left; ++i)
            if (!view.update(climb, 0.016f, terrain)) left = true;
        check("climbing leaves surface mode", left);

        // So must the B button, immediately.
        view.enter();
        Controls leave; leave.leave = true;
        check("B leaves surface mode at once", !view.update(leave, 0.016f, terrain));
    }

    std::printf("\n===== %d passed, %d failed =====\n", passN, failN);
    return failN ? 1 : 0;
}
