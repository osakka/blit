#!/bin/sh
set -eu

# Module gate, not presentation acceptance. Inputs are explicit so an arbitrary
# host font cannot silently stand in for the reviewed candidate assets.
if [ "$#" -ne 3 ] && [ "$#" -ne 4 ]; then
    echo "usage: sh font_raster_gate.sh PSF OUTLINE_TTF COLOUR_TTF [BELL_EM_BOUND]" >&2
    exit 2
fi
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
# FreeType and HarfBuzz from the system (tools/build-flags.sh); an
# assignment, so a missing or too-old module stops the gate.
cflags=$("$root/tools/build-flags.sh" --cflags font)
libs=$("$root/tools/build-flags.sh" --libs font)
work=$(mktemp -d /tmp/blit-raster-gate.XXXXXX)
# Only these validated, uniquely created temporary files are removed.
trap 'rm -f "$work/gate"; rmdir "$work"' EXIT HUP INT TERM
# shellcheck disable=SC2086
tcc -std=c99 -Wall $cflags "$root/tools/tests/font_raster_gate.c" \
    "$root/src/blit_raster.c" "$root/src/blit_font.c" $libs -o "$work/gate"
"$work/gate" "$@"
