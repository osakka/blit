#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
output=${1:-$ROOT/build/blit/blit-server}
version=$(sed -n '1p' "$ROOT/packaging/VERSION")

case "$version" in
    [0-9]*.[0-9]*.[0-9]*) ;;
    *) echo "blit: invalid version in packaging/VERSION" >&2; exit 1 ;;
esac

mkdir -p "$(dirname -- "$output")"
tcc -O2 -std=c99 \
    "-DBLIT_BUILD_VERSION=\"$version\"" \
    -I"$ROOT/vendor/SDL3/include" \
    -I"$ROOT/build/deps/prefix/include" \
    -I"$ROOT/build/deps/prefix/include/libdrm" \
    "$ROOT/src/blit_server.c" "$ROOT/src/blit_kms.c" \
    "$ROOT/src/blit_font.c" \
    -L"$ROOT/build/sdl3-kms" -lSDL3 \
    -L"$ROOT/build/deps/prefix/lib/x86_64-linux-gnu" -ldrm -lutil \
    -o "$output"
chmod 0755 "$output"
echo "built $output version $version with tcc"
