#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
mkdir -p "$root/build/tests"
cflags=$("$root/tools/build-flags.sh" --cflags font)
libs=$("$root/tools/build-flags.sh" --libs font)
# shellcheck disable=SC2086
tcc -std=c99 -Wall $cflags \
 "$root/tools/tests/modern_nerd_gate.c" "$root/src/blit_raster.c" \
 "$root/src/blit_font.c" $libs \
 -o "$root/build/tests/modern-nerd-gate"
"$root/build/tests/modern-nerd-gate" "$root/assets/fonts/Blit-Terminus16.psf" \
 "${1:-$root/assets/fonts/text/Hack-Regular.ttf}" "$root/assets/fonts/NotoColorEmoji.ttf"
