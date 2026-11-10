#!/bin/sh
# ADR 0226: frame-channel pictures with a held placement or an input route
# are never freed for room (graphics_memory_frames_gate.py).
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT"
. tools/tests/gate_display.sh
gate_display_setup
trap gate_display_cleanup EXIT
env $GATE_DISPLAY_ENV python3 tools/tests/graphics_memory_frames_gate.py
