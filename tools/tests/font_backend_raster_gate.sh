#!/bin/sh
# The KMS compositor's font raster path (ADR 0184: the only one).
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
# The gate runs twice in the font suite (default and retro outline); each run
# owns its binary so two of them can be in flight at once (ADR 0216).
work=$(mktemp -d "${TMPDIR:-/tmp}/blit-font-kms-raster.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
headers=${BLIT_FREETYPE_INCLUDE:-$root/build/deps/prefix/include/freetype2}
library=${BLIT_FREETYPE_LIBRARY:-/lib/x86_64-linux-gnu/libfreetype.so.6}
tcc -O2 -std=c99 \
 -I"$root/build/deps/prefix/include" -I"$root/build/deps/prefix/include/libdrm" \
 -I"$headers" "$root/tools/tests/font_kms_raster_gate.c" "$root/src/blit_font.c" \
 "$root/src/blit_raster.c" "$library" \
 "${BLIT_HARFBUZZ_LIBRARY:-/lib/x86_64-linux-gnu/libharfbuzz.so.0}" \
 -L"$root/build/deps/prefix/lib/x86_64-linux-gnu" -ldrm -lutil -lz \
 -o "$work/font-raster-kms"
export LD_LIBRARY_PATH="$root/build/deps/prefix/lib/x86_64-linux-gnu"
"$work/font-raster-kms" "$root/assets/fonts/Blit-Terminus16.psf" \
 "${BLIT_GATE_OUTLINE:-$root/assets/fonts/text/Hack-Regular.ttf}" "$root/assets/fonts/NotoColorEmoji.ttf"
