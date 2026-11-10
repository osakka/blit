#!/bin/sh
# ADR 0228 phase 1, milestone B: GET /v1/input/keymap compiles with
# libxkbcommon and types what Blit types, before and after a PATCH of the
# keyboard's keymap and modifiers. Needs xkbcommon (tools/build-flags.sh).
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT"
out=build/tests/keymap
mkdir -p "$out"
cflags=$(tools/build-flags.sh --cflags xkbcommon)
libs=$(tools/build-flags.sh --libs xkbcommon)
# shellcheck disable=SC2086
tcc -std=c99 -Wall $cflags tools/tests/xkb_keymap_check.c $libs -o "$out/xkb_keymap_check"
. tools/tests/gate_display.sh
gate_display_setup
trap gate_display_cleanup EXIT
BLIT_GATE_SERVER_ENV="$GATE_DISPLAY_ENV ${BLIT_GATE_SERVER_ENV:-}" python3 tools/tests/keymap_gate.py "$out/xkb_keymap_check" \
    "${BLIT_GATE_BINARY:-$ROOT/build/blit/blit-server}"
echo "KEYMAP_GATE_PASS"
