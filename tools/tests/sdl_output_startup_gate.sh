#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
mkdir -p "$root/build/tests"
tcc -O2 -std=c99 -I"$root/vendor/SDL3/include" \
 -I"$root/build/deps/prefix/include" -I"$root/build/deps/prefix/include/libdrm" \
 -I"$root/build/deps/prefix/include/freetype2" \
 "$root/tools/tests/sdl_output_startup_gate.c" "$root/src/blit_kms.c" \
 "$root/src/blit_font.c" "$root/src/blit_raster.c" \
 /lib/x86_64-linux-gnu/libfreetype.so.6 \
 -L"$root/build/sdl3-kms" -lSDL3 \
 -L"$root/build/deps/prefix/lib/x86_64-linux-gnu" -ldrm -lutil -lz \
 -o "$root/build/tests/sdl-output-startup-gate"
BLIT_TEST_RENDERER="${1:-software}" \
LD_LIBRARY_PATH="$root/build/sdl3-kms:$root/build/deps/prefix/lib/x86_64-linux-gnu" \
 "$root/build/tests/sdl-output-startup-gate" "$root/assets/fonts/Blit-Hibr-Terminus16.psf"
