#!/bin/sh
# The KMS compositor's font raster path (ADR 0184: the only one).
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
mkdir -p "$root/build/tests"
headers=${BLIT_FREETYPE_INCLUDE:-$root/build/deps/prefix/include/freetype2}
library=${BLIT_FREETYPE_LIBRARY:-/lib/x86_64-linux-gnu/libfreetype.so.6}
tcc -O2 -std=c99 \
 -I"$root/build/deps/prefix/include" -I"$root/build/deps/prefix/include/libdrm" \
 -I"$headers" "$root/tools/tests/font_kms_raster_gate.c" "$root/src/blit_font.c" \
 "$root/src/blit_raster.c" "$library" \
 "${BLIT_HARFBUZZ_LIBRARY:-/lib/x86_64-linux-gnu/libharfbuzz.so.0}" \
 -L"$root/build/deps/prefix/lib/x86_64-linux-gnu" -ldrm -lutil -lz \
 -o "$root/build/tests/font-raster-kms"
export LD_LIBRARY_PATH="$root/build/deps/prefix/lib/x86_64-linux-gnu"
"$root/build/tests/font-raster-kms" "$root/assets/fonts/Blit-Terminus16.psf" \
 "${BLIT_GATE_OUTLINE:-$root/assets/fonts/text/Hack-Regular.ttf}" "$root/assets/fonts/NotoColorEmoji.ttf"
