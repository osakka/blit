#!/bin/sh
set -eu

# Module gate: an unmapped scalar draws the font's '?' rather than nothing.
# blit_font_glyph_index needs no FreeType, so only blit_font.c is linked.
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d /tmp/blit-font-fallback-gate.XXXXXX)
# Only these validated, uniquely created temporary files are removed.
trap 'rm -f "$work/gate"; rmdir "$work"' EXIT HUP INT TERM
tcc -std=c99 -Wall -I"$root/src" "$root/tools/tests/font_fallback_gate.c" \
    "$root/src/blit_font.c" -o "$work/gate"
"$work/gate"
