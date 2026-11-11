#!/bin/sh
# ADR 0228 phase 2, milestones A and C: X programs in Hibr windows -- keys
# and mouse, menus, dialogs, states, move/resize, cursors -- blit-compositor
# with its Xwayland on, against a fake desktop, with the in-repo X stand-in
# client (tools/tests/xwayland_gate.py says what is checked). The
# compositor is built twice: by tcc, as shipped, and by cc under
# AddressSanitizer and UndefinedBehaviorSanitizer when the host's cc has
# them.
#
# It skips (77, XWAYLAND_GATE_SKIP <reason>) where the compositor's build
# dependencies or libxcb are missing (tools/build-flags.sh), or `Xwayland`
# is not on PATH or is older than 23.1 (xwayland_shell_v1).
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT"
out=build/tests/xwayland
proto=$out/protocol

skip() {
    echo "XWAYLAND_GATE_SKIP $*"
    exit 77
}

if ! why=$(tools/build-flags.sh --protocols 2>&1 >/dev/null) ||
        ! why=$(tools/build-flags.sh --plasma-protocols 2>&1 >/dev/null) ||
        ! why=$(tools/build-flags.sh --check compositor xcb 2>&1 >/dev/null); then
    skip "$why"
fi
xwayland=$(command -v Xwayland 2>/dev/null || true)
[ -n "$xwayland" ] || skip "Xwayland is not installed: no Xwayland on PATH (Debian: xwayland)"
version=$("$xwayland" -version 2>&1 | sed -n 's/.*Version \([0-9][0-9]*\)\.\([0-9][0-9]*\).*/\1 \2/p' | head -n 1)
[ -n "$version" ] || skip "$xwayland -version names no version"
major=${version% *}
minor=${version#* }
if [ "$major" -lt 23 ] || { [ "$major" -eq 23 ] && [ "$minor" -lt 1 ]; }; then
    skip "$xwayland is Xwayland $major.$minor, older than 23.1 (no xwayland_shell_v1)"
fi

mkdir -p "$out"
BLIT_PROTOCOL_DIR=$proto sh tools/build-tcc.sh "$out/blit-compositor" compositor >/dev/null
protocol_sources=$(cat "$proto/sources")
compositor_cflags=$(tools/build-flags.sh --cflags compositor)
compositor_libs=$(tools/build-flags.sh --libs compositor)
xcb_cflags=$(tools/build-flags.sh --cflags xcb)
xcb_libs=$(tools/build-flags.sh --libs xcb)
# shellcheck disable=SC2086
tcc -std=c99 -Wall $xcb_cflags tools/tests/x_standin_client.c $xcb_libs -o "$out/x_standin_client"

# blit-server, on the mock display, only for Blit's keymap.
. tools/tests/gate_display.sh
gate_display_setup
trap gate_display_cleanup EXIT
echo "XWAYLAND_GATE_XWAYLAND $xwayland $major.$minor"
BLIT_GATE_SERVER_ENV="$GATE_DISPLAY_ENV ${BLIT_GATE_SERVER_ENV:-}" python3 tools/tests/xwayland_gate.py \
    "$out/blit-compositor" "$out/x_standin_client" "${BLIT_GATE_BINARY:-$ROOT/build/blit/blit-server}"

san="-O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=all"
if ${CC:-cc} $san -x c -o "$out/san-probe" - >/dev/null 2>&1 <<'PROBE'
int main(void) { return 0; }
PROBE
then
    # shellcheck disable=SC2086
    ${CC:-cc} $san -std=c99 -Wall -Wno-unused-function -Isrc -I"$proto" $compositor_cflags \
        "-DBLIT_BUILD_VERSION=\"asan\"" src/blit_compositor.c $protocol_sources \
        $compositor_libs -o "$out/blit-compositor-asan"
    # As compositor_gate: without address randomisation (GCC 12's
    # AddressSanitizer under this kernel), which Xwayland inherits too.
    norand=
    if setarch -R true 2>/dev/null; then norand="setarch -R"; fi
    BLIT_GATE_SERVER_ENV="$GATE_DISPLAY_ENV ${BLIT_GATE_SERVER_ENV:-}" \
    ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 UBSAN_OPTIONS=print_stacktrace=1 \
        $norand python3 tools/tests/xwayland_gate.py "$out/blit-compositor-asan" "$out/x_standin_client" \
            "${BLIT_GATE_BINARY:-$ROOT/build/blit/blit-server}"
    echo "XWAYLAND_SANITIZERS_OK address,undefined"
else
    echo "XWAYLAND_SANITIZERS_SKIP ${CC:-cc} has no -fsanitize=address,undefined"
fi
echo "XWAYLAND_GATE_PASS"
