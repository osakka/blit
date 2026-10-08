#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
probe="$root/build/tests/sdl_glyph_filter_gate"
mkdir -p "$root/build/tests"
tcc -std=c99 -O2 -I"$root/vendor/SDL3/include" \
    "$root/tools/tests/sdl_glyph_filter_gate.c" \
    -L"$root/build/sdl3-kms" -lSDL3 -o "$probe"
SDL_VIDEODRIVER=dummy LD_LIBRARY_PATH="$root/build/sdl3-kms" "$probe"
