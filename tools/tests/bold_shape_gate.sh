#!/bin/sh
# ADR 0240: bold keeps a cell-filling glyph's shape, through the
# compositor's own cell path, for every shipped text font.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/blit-bold-shape.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
cflags=$("$root/tools/build-flags.sh" --cflags server)
libs=$("$root/tools/build-flags.sh" --libs server)
# shellcheck disable=SC2086
tcc -O2 -std=c99 $cflags \
 "$root/tools/tests/bold_shape_gate.c" "$root/src/blit_font.c" \
 "$root/src/blit_raster.c" $libs \
 -o "$work/bold-shape"
# The fonts as arguments (outline files, a directory of masters, or
# "pixel" for the pixel font's own raster); by default every shipped one.
# The runner splits them in two so each run stays well inside its limit.
[ "$#" -gt 0 ] || set -- "$root"/assets/fonts/text/*-Regular.ttf "$root/assets/fonts/text/spleen" pixel
BLIT_FONT_ROOT="$root/assets/fonts" "$work/bold-shape" "$root/assets/fonts/Blit-Terminus16.psf" \
 "$root/assets/fonts/NotoColorEmoji.ttf" "$@"
