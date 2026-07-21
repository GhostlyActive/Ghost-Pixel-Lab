#!/usr/bin/env bash
# Build and run the Ghost BASIC interpreter suite on the host.
#
# The screen/editor/BASIC sources carry no Arduino or board dependency, so they
# compile straight on a PC against the tiny stubs in hostshim/. That means the
# whole language can be tested in a second, without flashing anything.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
src="$here/../../src"
out="$(mktemp -d)"
trap 'rm -rf "$out"' EXIT

g++ -std=gnu++17 -Wall -Wextra -Wshadow \
    -I "$here/hostshim" -I "$src" \
    "$src/apps/ghost_basic/screen.cpp" \
    "$src/apps/ghost_basic/basic.cpp" \
    "$src/apps/ghost_basic/basic_expr.cpp" \
    "$src/apps/ghost_basic/editor.cpp" \
    "$here/suite.cpp" \
    -o "$out/suite"

"$out/suite"
