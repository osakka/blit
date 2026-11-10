#!/bin/sh
set -eu

# ADR 0220 module gate: the menu-bar state families differ by shape in the
# monochrome pens. Inputs are explicit, as for font_raster_gate.sh.
if [ "$#" -ne 3 ]; then
    echo "usage: sh font_icon_shape_gate.sh PSF OUTLINE_TTF COLOUR_TTF" >&2
    exit 2
fi
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
# FreeType and HarfBuzz from the system (tools/build-flags.sh); an
# assignment, so a missing or too-old module stops the gate.
cflags=$("$root/tools/build-flags.sh" --cflags font)
libs=$("$root/tools/build-flags.sh" --libs font)
work=$(mktemp -d /tmp/blit-icon-shape-gate.XXXXXX)
# Only these validated, uniquely created temporary files are removed.
trap 'rm -f "$work/gate"; rmdir "$work"' EXIT HUP INT TERM
# shellcheck disable=SC2086
tcc -std=c99 -Wall $cflags "$root/tools/tests/font_icon_shape_gate.c" \
    "$root/src/blit_raster.c" "$root/src/blit_font.c" $libs -o "$work/gate"
"$work/gate" "$@"
