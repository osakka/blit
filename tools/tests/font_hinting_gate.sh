#!/bin/sh
set -eu

# ADR 0218 module gate: the raster's hinting, with the default pinned to the
# pre-setting output. Inputs are explicit, as for font_raster_gate.sh.
if [ "$#" -ne 3 ]; then
    echo "usage: sh font_hinting_gate.sh PSF OUTLINE_TTF COLOUR_TTF" >&2
    exit 2
fi
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
headers=${BLIT_FREETYPE_INCLUDE:-$root/build/deps/prefix/include/freetype2}
library=${BLIT_FREETYPE_LIBRARY:-/lib/x86_64-linux-gnu/libfreetype.so.6}
test -f "$headers/ft2build.h"
test -f "$library"
work=$(mktemp -d /tmp/blit-hinting-gate.XXXXXX)
# Only these validated, uniquely created temporary files are removed.
trap 'rm -f "$work/gate"; rmdir "$work"' EXIT HUP INT TERM
tcc -std=c99 -Wall -I"$headers" "$root/tools/tests/font_hinting_gate.c" \
    "$root/src/blit_raster.c" "$root/src/blit_font.c" "$library" \
    "${BLIT_HARFBUZZ_LIBRARY:-/lib/x86_64-linux-gnu/libharfbuzz.so.0}" -o "$work/gate"
"$work/gate" "$@"
