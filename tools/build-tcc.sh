#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
output=${1:-$ROOT/build/blit/blit-server}
target=${2:-server}
version=$(sed -n '1p' "$ROOT/packaging/VERSION")
hibr_version=$(sed -n '1p' "$ROOT/packaging/HIBR_VERSION")
hibr_abi=$(sed -n '1p' "$ROOT/packaging/HIBR_ABI")

case "$version" in
    [0-9]*.[0-9]*.[0-9]*) ;;
    *) echo "blit: invalid version in packaging/VERSION" >&2; exit 1 ;;
esac
case "$hibr_version" in
    [0-9]*.[0-9]*.[0-9]*) ;;
    *) echo "blit: invalid version in packaging/HIBR_VERSION" >&2; exit 1 ;;
esac
case "$hibr_abi" in
    ''|*[!0-9]*) echo "blit: invalid ABI in packaging/HIBR_ABI" >&2; exit 1 ;;
esac

mkdir -p "$(dirname -- "$output")"
case "$target" in
    server)
        tcc -O2 -g -bt8 -std=c99 \
            "-DBLIT_BUILD_VERSION=\"$version\"" \
            "-DBLIT_HIBR_VERSION=\"$hibr_version\"" \
            "-DBLIT_HIBR_ABI=$hibr_abi" \
            -I"$ROOT/build/deps/prefix/include" \
            -I"$ROOT/build/deps/prefix/include/libdrm" \
            -I"${BLIT_FREETYPE_INCLUDE:-$ROOT/build/deps/prefix/include/freetype2}" \
            "$ROOT/src/blit_server.c" "$ROOT/src/blit_kms.c" "$ROOT/src/blit_vt.c" \
            "$ROOT/src/blit_font.c" "$ROOT/src/blit_raster.c" \
            "${BLIT_FREETYPE_LIBRARY:-/lib/x86_64-linux-gnu/libfreetype.so.6}" \
            "${BLIT_HARFBUZZ_LIBRARY:-/lib/x86_64-linux-gnu/libharfbuzz.so.0}" \
            -L"$ROOT/build/deps/prefix/lib/x86_64-linux-gnu" -ldrm -lutil -lz \
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
        expected_hibr_version=$(sed -n '1p' "$ROOT/packaging/HIBR_VERSION")
        actual_hibr_version=$(sed -n 's/^#define HIBR_VER "\(.*\)"/\1/p' \
            "$hibr_root/include/hibr.h" | head -n 1)
        actual_hibr_abi=$(sed -n 's/^#define HIBR_ABI \(.*\)u/\1/p' \
            "$hibr_root/include/hibr.h" | head -n 1)
        if [ -z "$expected_hibr_version" ] ||
           [ "$actual_hibr_version" != "$expected_hibr_version" ]; then
            echo "blit: Hibr adapter target mismatch: expected $expected_hibr_version, found $actual_hibr_version under $hibr_root" >&2
            exit 1
        fi
        if [ "$actual_hibr_abi" != "$hibr_abi" ]; then
            echo "blit: Hibr adapter ABI mismatch: expected $hibr_abi, found $actual_hibr_abi under $hibr_root" >&2
            exit 1
        fi
        tcc -O2 -std=c99 -shared -fPIC \
            -I"$hibr_root/include" -I"$hibr_root/mods" \
            -o "$output" "$ROOT/hibr/console-blit.c"
        ;;
    gamescope)
        # ADR 0225: blit-gamescope. The PipeWire/SPA headers need GNU C, so
        # this target is built by cc, not tcc; linked by SONAME. The headers
        # are Debian 13's libpipewire-0.3-dev and libspa-0.2-dev 1.4.2, kept
        # in build/deps/prefix like the other build dependencies
        # (docs/toolchain.md).
        pw_include=${BLIT_PIPEWIRE_INCLUDE:-$ROOT/build/deps/prefix/include/pipewire-0.3}
        spa_include=${BLIT_SPA_INCLUDE:-$ROOT/build/deps/prefix/include/spa-0.2}
        if [ ! -f "$pw_include/pipewire/pipewire.h" ] || [ ! -f "$spa_include/spa/pod/builder.h" ]; then
            echo "blit: PipeWire headers not found (set BLIT_PIPEWIRE_INCLUDE and BLIT_SPA_INCLUDE)" >&2
            exit 1
        fi
        # The package build needs the same bytes from any directory and any
        # checkout: -g records the compiler's working directory and the
        # source paths, so compile from the root and record it as ".".
        case "$output" in
            /*) ;;
            *) output=$(pwd)/$output ;;
        esac
        (cd "$ROOT" && ${CC:-cc} -O2 -g -std=gnu99 -Wall -Wno-unused-function \
            -ffile-prefix-map="$ROOT"=. \
            "-DBLIT_BUILD_VERSION=\"$version\"" \
            -I"$ROOT/src" -I"$pw_include" -I"$spa_include" \
            "$ROOT/src/blit_gamescope.c" \
            "${BLIT_PIPEWIRE_LIBRARY:-/lib/x86_64-linux-gnu/libpipewire-0.3.so.0}" \
            -o "$output")
        chmod 0755 "$output"
        echo "built $output target=$target version $version with ${CC:-cc}"
        exit 0
        ;;
    *)
        echo "blit: unknown TCC target '$target' (use server, helper, adapter, or gamescope)" >&2
        exit 1
        ;;
esac
chmod 0755 "$output"
echo "built $output target=$target version $version with tcc"
