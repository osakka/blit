#!/bin/sh
# ADR 0228 phase 1, milestone A: blit-compositor (the Wayland server, the
# WINDOW wire, wl_shm frames as FRM1) against a fake desktop, with the
# in-repo stand-in client; then Blit's own side of a version 4 hello on the
# mock display. The compositor and the stand-in are built twice: by tcc, as
# shipped, and by cc under AddressSanitizer and UndefinedBehaviorSanitizer
# when the host's cc has them.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT"
out=build/tests/compositor
proto=$out/protocol
# Version 5 of the frame channel only (docs/api.md, "One grammar, version
# 5"): none of the version 4 line forms is read or written by the
# compositor, and its hello says 5.
if grep -nE '"(RELEASE|ERROR|OK input|OK window)|"ERROR (INPUT|WINDOW)|INPUT sock %s' \
        src/blit_compositor.c src/blit_compositor_wire.h ||
        ! grep -q '^#define BC_WIRE_VERSION 5$' src/blit_compositor_wire.h; then
    echo "COMPOSITOR_GATE_FAIL blit-compositor still speaks a version 4 line form" >&2
    exit 1
fi
# The compositor's dependencies (tools/build-flags.sh): a host below the
# minimums -- wayland-protocols before 1.32 has no cursor-shape-v1 -- is a
# blocked capability, a skip that says why, never a fallback.
if ! why=$(tools/build-flags.sh --protocols 2>&1 >/dev/null) ||
        ! why=$(tools/build-flags.sh --plasma-protocols 2>&1 >/dev/null) ||
        ! why=$(tools/build-flags.sh --check compositor wayland-client 2>&1 >/dev/null); then
    echo "COMPOSITOR_GATE_SKIP $why"
    exit 77
fi
mkdir -p "$out"
BLIT_PROTOCOL_DIR=$proto sh tools/build-tcc.sh "$out/blit-compositor" compositor >/dev/null
# The protocol code build-tcc.sh generated, for the client and the
# sanitized builds alike.
protocol_sources=$(cat "$proto/sources")
client_sources="tools/tests/wl_standin_client.c $protocol_sources"
# The system's libwayland and libxkbcommon (tools/build-flags.sh).
client_cflags=$(tools/build-flags.sh --cflags wayland-client)
client_libs=$(tools/build-flags.sh --libs wayland-client)
compositor_cflags=$(tools/build-flags.sh --cflags compositor)
compositor_libs=$(tools/build-flags.sh --libs compositor)
# shellcheck disable=SC2086
tcc -std=c99 -Wall -I"$proto" $client_cflags $client_sources $client_libs -o "$out/wl_standin_client"
tcc -Wall -Isrc tools/tests/compositor_wire_unit.c -o "$out/compositor_wire_unit"
"$out/compositor_wire_unit"

. tools/tests/gate_display.sh
gate_display_setup
trap gate_display_cleanup EXIT
BLIT_GATE_SERVER_ENV="$GATE_DISPLAY_ENV ${BLIT_GATE_SERVER_ENV:-}" python3 tools/tests/compositor_gate.py "$out/blit-compositor" \
    "$out/wl_standin_client" "${BLIT_GATE_BINARY:-$ROOT/build/blit/blit-server}"

san="-O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=all"
if ${CC:-cc} $san -x c -o "$out/san-probe" - >/dev/null 2>&1 <<'PROBE'
int main(void) { return 0; }
PROBE
then
    # shellcheck disable=SC2086
    ${CC:-cc} $san -std=c99 -Wall -Wno-unused-function -Isrc -I"$proto" $(cat "$proto/include") $compositor_cflags \
        "-DBLIT_BUILD_VERSION=\"asan\"" src/blit_compositor.c $protocol_sources \
        $compositor_libs -lm -o "$out/blit-compositor-asan"
    # shellcheck disable=SC2086
    ${CC:-cc} $san -std=c99 -Wall -I"$proto" $client_cflags $client_sources $client_libs \
        -o "$out/wl_standin_client-asan"
    # shellcheck disable=SC2086
    ${CC:-cc} $san -std=c99 -Wall -Wno-unused-function -Isrc tools/tests/compositor_wire_unit.c \
        -o "$out/compositor_wire_unit-asan"
    # GCC 12's AddressSanitizer cannot map its shadow under this kernel's
    # wider mmap randomisation and spins on DEADLYSIGNAL at random: the
    # sanitized runs are without address randomisation (setarch -R), which
    # their children inherit.
    norand=
    if setarch -R true 2>/dev/null; then norand="setarch -R"; fi
    $norand "$out/compositor_wire_unit-asan"
    # The mock display is preloaded into blit-server only: AddressSanitizer
    # must come first in the sanitized programs.
    BLIT_GATE_SERVER_ENV="$GATE_DISPLAY_ENV ${BLIT_GATE_SERVER_ENV:-}" \
    ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 UBSAN_OPTIONS=print_stacktrace=1 \
        $norand python3 tools/tests/compositor_gate.py "$out/blit-compositor-asan" "$out/wl_standin_client-asan" \
            "${BLIT_GATE_BINARY:-$ROOT/build/blit/blit-server}"
    echo "COMPOSITOR_SANITIZERS_OK address,undefined"
else
    echo "COMPOSITOR_SANITIZERS_SKIP ${CC:-cc} has no -fsanitize=address,undefined"
fi
echo "COMPOSITOR_GATE_PASS"
