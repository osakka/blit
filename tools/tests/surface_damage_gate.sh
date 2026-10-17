#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
mkdir -p "$root/build/tests"
bin="$root/build/tests/surface_damage_gate"
tcc -O2 -std=c99 -I"$root/vendor/SDL3/include" \
    -I"$root/build/deps/prefix/include" \
    -I"$root/build/deps/prefix/include/libdrm" \
    "$root/tools/tests/surface_damage_gate.c" \
    "$root/src/blit_kms.c" "$root/src/blit_font.c" \
    -L"$root/build/sdl3-kms" -lSDL3 \
    -L"$root/build/deps/prefix/lib/x86_64-linux-gnu" -ldrm -lutil -lz \
    -o "$bin"
LD_LIBRARY_PATH="$root/build/sdl3-kms:$root/build/deps/prefix/lib/x86_64-linux-gnu" "$bin"
