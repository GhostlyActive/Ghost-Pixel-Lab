#!/usr/bin/env bash
# Build and run the Outer Pixels suite on the host.
#
# Everything under src/apps/outer_pixels/ is free of Arduino and board
# dependencies — the only thing it needs from the device is board::gfx::Surface,
# which ../hostshim stands in for. So the orbits, the flight model, the terrain
# generator and both renderers all compile straight on a PC, and a broken split
# shows up here in a second instead of after a flash.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
src="$here/../../src"
out="$(mktemp -d)"
trap 'rm -rf "$out"' EXIT

g++ -std=gnu++17 -Wall -Wextra -Wshadow \
    -I "$here/../hostshim" -I "$src" \
    "$src/apps/outer_pixels/bodies.cpp" \
    "$src/apps/outer_pixels/terrain.cpp" \
    "$src/apps/outer_pixels/ship.cpp" \
    "$src/apps/outer_pixels/clouds.cpp" \
    "$src/apps/outer_pixels/space_view.cpp" \
    "$src/apps/outer_pixels/surface_view.cpp" \
    "$here/suite.cpp" \
    -o "$out/suite"

"$out/suite"
