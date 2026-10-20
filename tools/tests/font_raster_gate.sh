#!/bin/sh
set -eu

# Module gate, not presentation acceptance. Inputs are explicit so an arbitrary
# host font cannot silently stand in for the reviewed candidate assets.
if [ "$#" -ne 3 ] && [ "$#" -ne 4 ]; then
    echo "usage: sh font_raster_gate.sh PSF OUTLINE_TTF COLOUR_TTF [BELL_EM_BOUND]" >&2
    exit 2
fi
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
headers=${BLIT_FREETYPE_INCLUDE:-$root/build/deps/prefix/include/freetype2}
library=${BLIT_FREETYPE_LIBRARY:-/lib/x86_64-linux-gnu/libfreetype.so.6}
test -f "$headers/ft2build.h"
test -f "$library"
work=$(mktemp -d /tmp/blit-raster-gate.XXXXXX)
# Only these validated, uniquely created temporary files are removed.
trap 'rm -f "$work/gate"; rmdir "$work"' EXIT HUP INT TERM
tcc -std=c99 -Wall -I"$headers" "$root/tools/tests/font_raster_gate.c" \
    "$root/src/blit_raster.c" "$root/src/blit_font.c" "$library" \
    "${BLIT_HARFBUZZ_LIBRARY:-/lib/x86_64-linux-gnu/libharfbuzz.so.0}" -o "$work/gate"
"$work/gate" "$@"
