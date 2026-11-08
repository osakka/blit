#!/bin/sh
# ADR 0226: frame-channel pictures with a held placement or an input route
# are never freed for room (graphics_memory_frames_gate.py), against the
# stand-in libei as frame_input_gate.sh uses it.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT"
mkdir -p build/tests/libei-stub
tcc -shared -fPIC -Wall tools/tests/fixtures/libei_stub.c -o build/tests/libei-stub/libei.so.1
. tools/tests/gate_display.sh
gate_display_setup
trap gate_display_cleanup EXIT
env $GATE_DISPLAY_ENV python3 tools/tests/graphics_memory_frames_gate.py
