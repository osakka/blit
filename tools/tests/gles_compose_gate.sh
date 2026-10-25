#!/bin/sh
# ADR 0181 step 3: GPU view composition against the CPU compositor.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
bin=${TMPDIR:-/tmp}/blit-gles-compose-$$
trap 'rm -f "$bin"' EXIT
cd "$ROOT"
tcc -O2 -std=c99 -I"$ROOT/build/deps/prefix/include" \
    -I"$ROOT/build/deps/prefix/include/libdrm" -I"$ROOT/build/deps/prefix/include/freetype2" \
    tools/tests/gles_compose_gate.c src/blit_font.c src/blit_raster.c \
    /lib/x86_64-linux-gnu/libfreetype.so.6 /lib/x86_64-linux-gnu/libharfbuzz.so.0 \
    -L"$ROOT/build/deps/prefix/lib/x86_64-linux-gnu" -ldrm -lz -o "$bin"
LD_LIBRARY_PATH="$ROOT/build/deps/prefix/lib/x86_64-linux-gnu" "$bin"
