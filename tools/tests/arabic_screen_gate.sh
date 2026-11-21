#!/bin/sh
# ADR 0241: a screen of distinct Arabic words on the GLES compositor stays in
# its caches after the first frame (tools/tests/arabic_screen_gate.c).
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
bin=${TMPDIR:-/tmp}/blit-arabic-screen-$$
trap 'rm -f "$bin"' EXIT
cd "$ROOT"
cflags=$(tools/build-flags.sh --cflags drm font zlib)
libs=$(tools/build-flags.sh --libs drm font zlib)
# shellcheck disable=SC2086
tcc -O2 -std=c99 $cflags tools/tests/arabic_screen_gate.c src/blit_font.c src/blit_raster.c \
    $libs -o "$bin"
BLIT_FONT_ROOT="$ROOT/assets/fonts" "$bin"
