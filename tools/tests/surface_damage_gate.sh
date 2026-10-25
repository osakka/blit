#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
mkdir -p "$root/build/tests"
bin="$root/build/tests/surface_damage_gate"
tcc -O2 -std=c99 \
    -I"$root/build/deps/prefix/include" \
    -I"$root/build/deps/prefix/include/libdrm" \
    -I"$root/build/deps/prefix/include/freetype2" \
    "$root/tools/tests/surface_damage_gate.c" \
    "$root/src/blit_kms.c" "$root/src/blit_vt.c" "$root/src/blit_font.c" "$root/src/blit_raster.c" \
    /lib/x86_64-linux-gnu/libfreetype.so.6 \
    "${BLIT_HARFBUZZ_LIBRARY:-/lib/x86_64-linux-gnu/libharfbuzz.so.0}" \
    -L"$root/build/deps/prefix/lib/x86_64-linux-gnu" -ldrm -lutil -lz \
    -o "$bin"
LD_LIBRARY_PATH="$root/build/deps/prefix/lib/x86_64-linux-gnu" "$bin"
