#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
mkdir -p "$root/build/tests"
headers=${BLIT_FREETYPE_INCLUDE:-$root/build/deps/prefix/include/freetype2}
library=${BLIT_FREETYPE_LIBRARY:-/lib/x86_64-linux-gnu/libfreetype.so.6}
mode=${1:-software}
case "$mode" in
 kms) source=font_kms_raster_gate.c; server_sources= ;;
 software|opengles2) source=font_sdl_raster_gate.c; server_sources="$root/src/blit_kms.c" ;;
 *) exit 2 ;;
esac
tcc -O2 -std=c99 -I"$root/vendor/SDL3/include" \
 -I"$root/build/deps/prefix/include" -I"$root/build/deps/prefix/include/libdrm" \
 -I"$headers" "$root/tools/tests/$source" "$root/src/blit_font.c" \
 "$root/src/blit_raster.c" $server_sources "$library" \
 -L"$root/build/sdl3-kms" -lSDL3 \
 -L"$root/build/deps/prefix/lib/x86_64-linux-gnu" -ldrm -lutil -lz \
 -o "$root/build/tests/font-raster-$mode"
export LD_LIBRARY_PATH="$root/build/sdl3-kms:$root/build/deps/prefix/lib/x86_64-linux-gnu"
case "$mode" in
 software) export SDL_VIDEODRIVER=dummy BLIT_TEST_RENDERER=software ;;
 opengles2) export SDL_VIDEODRIVER=offscreen BLIT_TEST_RENDERER=opengles2 LIBGL_ALWAYS_SOFTWARE=1 ;;
esac
"$root/build/tests/font-raster-$mode" "$root/assets/fonts/Blit-Hibr-Terminus16.psf" \
 "$root/assets/fonts/BlitModernNerd-Regular.ttf" "$root/assets/fonts/NotoColorEmoji.ttf"
