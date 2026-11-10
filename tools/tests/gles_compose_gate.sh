#!/bin/sh
# ADR 0181 step 3: GPU view composition against the CPU compositor.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
bin=${TMPDIR:-/tmp}/blit-gles-compose-$$
trap 'rm -f "$bin"' EXIT
cd "$ROOT"
cflags=$(tools/build-flags.sh --cflags drm font zlib)
libs=$(tools/build-flags.sh --libs drm font zlib)
# shellcheck disable=SC2086
tcc -O2 -std=c99 $cflags \
    tools/tests/gles_compose_gate.c src/blit_font.c src/blit_raster.c \
    $libs -o "$bin"
BLIT_FONT_ROOT="$ROOT/assets/fonts" "$bin"
