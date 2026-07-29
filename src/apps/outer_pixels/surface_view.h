// Low-altitude flight over a planet: a Comanche-style voxel renderer over the
// procedural heightmap, plus the camera that moves through it.
//
// No polygons and no depth buffer — the terrain is drawn as columns, marching
// away from the camera and painting each screen column only above whatever the
// nearer slices already covered. That single "highest so far" array per column
// is the whole hidden-surface algorithm, and it is why this runs at all on a
// 240 MHz chip.
#pragma once

#include "controls.h"
#include "terrain.h"
#include "bodies.h"
#include "board/surface.h"

namespace apps::outer {

class SurfaceView {
public:
    // Drop in over the terrain. Starts low so there is landscape in frame
    // immediately rather than an empty horizon.
    void enter();

    // One frame of flight. Returns false when the camera has climbed out of
    // the atmosphere or the player asked to leave — the app then switches back
    // to space. `pitch` in the controls carries the climb axis here.
    bool update(const Controls& c, float dt, const Terrain& terrain);

    void render(board::gfx::Surface& s, const Terrain& terrain,
                const Body& planet) const;

    [[nodiscard]] float altitude() const { return alt_; }

private:
    float x_ = 0, y_ = 0;      // position on the wrapping terrain tile
    float alt_ = 0;            // height above the tile's zero plane
    float yaw_ = 0;            // heading
    float pitch_ = 0;          // -1..1, tilts the horizon and drives the climb
};

} // namespace apps::outer
