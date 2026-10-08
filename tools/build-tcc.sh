#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
output=${1:-$ROOT/build/blit/blit-server}
target=${2:-server}
version=$(sed -n '1p' "$ROOT/packaging/VERSION")

case "$version" in
    [0-9]*.[0-9]*.[0-9]*) ;;
    *) echo "blit: invalid version in packaging/VERSION" >&2; exit 1 ;;
esac

mkdir -p "$(dirname -- "$output")"
case "$target" in
    server)
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
        ;;
    helper)
        tcc -O2 -std=c99 -o "$output" "$ROOT/src/blit_update_helper.c"
        ;;
    adapter)
        hibr_root=${BLIT_HIBR_ROOT:-}
        if [ -z "$hibr_root" ]; then
            echo "blit: BLIT_HIBR_ROOT is required for the Hibr adapter" >&2
            exit 1
        fi
        if [ ! -d "$hibr_root/include" ] || [ ! -d "$hibr_root/mods" ]; then
            echo "blit: Hibr headers/modules not found under $hibr_root" >&2
            exit 1
        fi
        tcc -O2 -std=c99 -shared -fPIC \
            -I"$hibr_root/include" -I"$hibr_root/mods" \
            -o "$output" "$ROOT/hibr/console-blit.c"
        ;;
    *)
        echo "blit: unknown TCC target '$target' (use server, helper, or adapter)" >&2
        exit 1
        ;;
esac
chmod 0755 "$output"
echo "built $output target=$target version $version with tcc"
