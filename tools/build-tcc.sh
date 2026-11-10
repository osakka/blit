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
        # Dependencies from the system through pkg-config, minimums checked
        # (tools/build-flags.sh, docs/toolchain.md). An assignment, so a
        # missing or too-old module stops the build here.
        cflags=$("$ROOT/tools/build-flags.sh" --cflags server)
        libs=$("$ROOT/tools/build-flags.sh" --libs server)
        # shellcheck disable=SC2086
        tcc -O2 -g -bt8 -std=c99 \
            "-DBLIT_BUILD_VERSION=\"$version\"" \
            "-DBLIT_HIBR_VERSION=\"$hibr_version\"" \
            "-DBLIT_HIBR_ABI=$hibr_abi" \
            $cflags \
            "$ROOT/src/blit_server.c" "$ROOT/src/blit_kms.c" "$ROOT/src/blit_vt.c" \
            "$ROOT/src/blit_font.c" "$ROOT/src/blit_raster.c" \
            $libs \
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
    compositor)
        # ADR 0228: blit-compositor, Blit's Wayland compositor for programs
        # and the X window manager of its Xwayland, its own program linked
        # against libwayland-server, libxkbcommon and libxcb (with its
        # composite, xfixes and res extensions).
        # Its protocol code is generated here by the system's wayland-scanner
        # from the system's wayland-protocols XML, both found through
        # pkg-config (tools/build-flags.sh), never committed: server code
        # for the compositor and client code for the in-repo stand-in client
        # (tools/tests/wl_standin_client.c), both in BLIT_PROTOCOL_DIR (the
        # output directory, default build/protocol). The XML bounds the
        # versions offered (blit_compositor.c, BC_VERSION).
        protocols=$("$ROOT/tools/build-flags.sh" --protocols)
        plasma=$("$ROOT/tools/build-flags.sh" --plasma-protocols)
        scanner=$("$ROOT/tools/build-flags.sh" --scanner)
        cflags=$("$ROOT/tools/build-flags.sh" --cflags compositor)
        libs=$("$ROOT/tools/build-flags.sh" --libs compositor)
        proto=${BLIT_PROTOCOL_DIR:-$ROOT/build/protocol}
        mkdir -p "$proto"
        # The protocols the compositor speaks: this list is the one place.
        # Their code goes to $proto, and $proto/sources lists the .c files
        # for every other build of it (the gate's client and sanitizer
        # builds). cursor-shape-v1 refers to the tablet tool, so the tablet
        # protocol's interfaces are generated with it. xwayland-shell-v1
        # (staging since 1.31) is how Xwayland names each X window's surface.
        : > "$proto/sources"
        for xml in "$protocols/stable/xdg-shell/xdg-shell.xml" \
                   "$protocols/unstable/xdg-decoration/xdg-decoration-unstable-v1.xml" \
                   "$protocols/unstable/linux-dmabuf/linux-dmabuf-unstable-v1.xml" \
                   "$protocols/stable/presentation-time/presentation-time.xml" \
                   "$protocols/unstable/relative-pointer/relative-pointer-unstable-v1.xml" \
                   "$protocols/unstable/pointer-constraints/pointer-constraints-unstable-v1.xml" \
                   "$protocols/unstable/primary-selection/primary-selection-unstable-v1.xml" \
                   "$protocols/staging/cursor-shape/cursor-shape-v1.xml" \
                   "$protocols/unstable/tablet/tablet-unstable-v2.xml" \
                   "$protocols/staging/xwayland-shell/xwayland-shell-v1.xml" \
                   "$plasma/appmenu.xml"; do
            base=$(basename "$xml" .xml)
            "$scanner" server-header "$xml" "$proto/$base-server-protocol.h"
            "$scanner" client-header "$xml" "$proto/$base-client-protocol.h"
            "$scanner" private-code "$xml" "$proto/$base-protocol.c"
            echo "$proto/$base-protocol.c" >> "$proto/sources"
        done
        # No -g: the package (packaging/build-deb.sh, blit-compositor) must
        # be the same bytes from any checkout, and tcc 0.9.27 records the
        # compiler's working directory and each source's path in its stabs
        # and has no -ffile-prefix-map (it accepts the flag and ignores it,
        # measured: the path stays in the binary). Without debug
        # information nothing records a path.
        # shellcheck disable=SC2086
        tcc -O2 -std=c99 \
            "-DBLIT_BUILD_VERSION=\"$version\"" \
            -I"$ROOT/src" -I"$proto" $cflags \
            "$ROOT/src/blit_compositor.c" $(cat "$proto/sources") \
            $libs \
            -o "$output"
        ;;
    *)
        echo "blit: unknown TCC target '$target' (use server, helper, adapter, or compositor)" >&2
        exit 1
        ;;
esac
chmod 0755 "$output"
echo "built $output target=$target version $version with tcc"
