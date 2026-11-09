#!/bin/sh
# ADR 0228 phase 1, milestone B: GET /v1/input/keymap compiles with
# libxkbcommon and types what Blit types, before and after a PATCH of the
# keyboard's keymap and modifiers. Needs libxkbcommon-dev on the build host.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT"
out=build/tests/keymap
mkdir -p "$out"
tcc -std=c99 -Wall tools/tests/xkb_keymap_check.c -lxkbcommon -o "$out/xkb_keymap_check"
. tools/tests/gate_display.sh
gate_display_setup
trap gate_display_cleanup EXIT
env $GATE_DISPLAY_ENV python3 tools/tests/keymap_gate.py "$out/xkb_keymap_check" \
    "${BLIT_GATE_BINARY:-$ROOT/build/blit/blit-server}"
echo "KEYMAP_GATE_PASS"
