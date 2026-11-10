#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
mkdir -p "$root/build/tests"
bin="$root/build/tests/surface_damage_gate"
cflags=$("$root/tools/build-flags.sh" --cflags server)
libs=$("$root/tools/build-flags.sh" --libs server)
# shellcheck disable=SC2086
tcc -O2 -std=c99 $cflags \
    "$root/tools/tests/surface_damage_gate.c" \
    "$root/src/blit_kms.c" "$root/src/blit_vt.c" "$root/src/blit_font.c" "$root/src/blit_raster.c" \
    $libs \
    -o "$bin"
"$bin"
