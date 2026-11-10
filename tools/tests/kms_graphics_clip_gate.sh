#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
bin=$(mktemp /tmp/blit-kms-clip.XXXXXX)
trap 'rm -f "$bin"' EXIT HUP INT TERM
cflags=$("$root/tools/build-flags.sh" --cflags drm zlib)
libs=$("$root/tools/build-flags.sh" --libs drm zlib)
# shellcheck disable=SC2086
tcc -O2 -std=c99 $cflags \
    "$root/tools/tests/kms_graphics_clip_gate.c" "$root/src/blit_font.c" \
    $libs -o "$bin"
"$bin"
