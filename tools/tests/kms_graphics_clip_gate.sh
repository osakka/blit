#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
bin=$(mktemp /tmp/blit-kms-clip.XXXXXX)
trap 'rm -f "$bin"' EXIT HUP INT TERM
tcc -O2 -std=c99 -I"$root/vendor/SDL3/include" \
    -I"$root/build/deps/prefix/include" -I"$root/build/deps/prefix/include/libdrm" \
    "$root/tools/tests/kms_graphics_clip_gate.c" "$root/src/blit_font.c" \
    -L"$root/build/deps/prefix/lib/x86_64-linux-gnu" -ldrm -lz -o "$bin"
LD_LIBRARY_PATH="$root/build/deps/prefix/lib/x86_64-linux-gnu" "$bin"
