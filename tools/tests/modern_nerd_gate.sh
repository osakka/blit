#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
mkdir -p "$root/build/tests"
tcc -std=c99 -Wall -I"$root/build/deps/prefix/include/freetype2" \
 "$root/tools/tests/modern_nerd_gate.c" "$root/src/blit_raster.c" \
 "$root/src/blit_font.c" /lib/x86_64-linux-gnu/libfreetype.so.6 \
 "${BLIT_HARFBUZZ_LIBRARY:-/lib/x86_64-linux-gnu/libharfbuzz.so.0}" \
 -o "$root/build/tests/modern-nerd-gate"
"$root/build/tests/modern-nerd-gate" "$root/assets/fonts/Blit-Terminus16.psf" \
 "${1:-$root/assets/fonts/BlitModernNerd-Regular.ttf}" "$root/assets/fonts/NotoColorEmoji.ttf"
