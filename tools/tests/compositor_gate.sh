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
mkdir -p "$out"
BLIT_PROTOCOL_DIR=$proto sh tools/build-tcc.sh "$out/blit-compositor" compositor >/dev/null
# The protocol code build-tcc.sh generated, for the client and the
# sanitized builds alike.
protocol_sources="$proto/xdg-shell-protocol.c $proto/xdg-decoration-unstable-v1-protocol.c
    $proto/linux-dmabuf-unstable-v1-protocol.c $proto/presentation-time-protocol.c"
client_sources="tools/tests/wl_standin_client.c $protocol_sources"
# shellcheck disable=SC2086
tcc -std=c99 -Wall -I"$proto" $client_sources -lwayland-client -o "$out/wl_standin_client"
tcc -Wall -Isrc tools/tests/compositor_wire_unit.c -o "$out/compositor_wire_unit"
"$out/compositor_wire_unit"

. tools/tests/gate_display.sh
gate_display_setup
trap gate_display_cleanup EXIT
env $GATE_DISPLAY_ENV python3 tools/tests/compositor_gate.py "$out/blit-compositor" \
    "$out/wl_standin_client" "${BLIT_GATE_BINARY:-$ROOT/build/blit/blit-server}"

san="-O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=all"
if ${CC:-cc} $san -x c -o "$out/san-probe" - >/dev/null 2>&1 <<'PROBE'
int main(void) { return 0; }
PROBE
then
    # shellcheck disable=SC2086
    ${CC:-cc} $san -std=c99 -Wall -Wno-unused-function -Isrc -I"$proto" \
        "-DBLIT_BUILD_VERSION=\"asan\"" src/blit_compositor.c $protocol_sources \
        -lwayland-server -o "$out/blit-compositor-asan"
    # shellcheck disable=SC2086
    ${CC:-cc} $san -std=c99 -Wall -I"$proto" $client_sources -lwayland-client \
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
    ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 UBSAN_OPTIONS=print_stacktrace=1 \
        $norand python3 tools/tests/compositor_gate.py "$out/blit-compositor-asan" "$out/wl_standin_client-asan"
    echo "COMPOSITOR_SANITIZERS_OK address,undefined"
else
    echo "COMPOSITOR_SANITIZERS_SKIP ${CC:-cc} has no -fsanitize=address,undefined"
fi
echo "COMPOSITOR_GATE_PASS"
