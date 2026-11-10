#!/bin/sh
set -eu

# ADR 0218 module gate: the raster's hinting, with the default pinned to the
# pre-setting output. Inputs are explicit, as for font_raster_gate.sh.
if [ "$#" -ne 3 ]; then
    echo "usage: sh font_hinting_gate.sh PSF OUTLINE_TTF COLOUR_TTF" >&2
    exit 2
fi
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
# FreeType and HarfBuzz from the system (tools/build-flags.sh); an
# assignment, so a missing or too-old module stops the gate.
cflags=$("$root/tools/build-flags.sh" --cflags font)
libs=$("$root/tools/build-flags.sh" --libs font)
work=$(mktemp -d /tmp/blit-hinting-gate.XXXXXX)
# Only these validated, uniquely created temporary files are removed.
trap 'rm -f "$work/gate"; rmdir "$work"' EXIT HUP INT TERM
# shellcheck disable=SC2086
tcc -std=c99 -Wall $cflags "$root/tools/tests/font_hinting_gate.c" \
    "$root/src/blit_raster.c" "$root/src/blit_font.c" $libs -o "$work/gate"
"$work/gate" "$@"
