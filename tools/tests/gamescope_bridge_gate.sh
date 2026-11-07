#!/bin/sh
# ADR 0225: blit-gamescope's frame and terminal cores, without gamescope,
# PipeWire or a GPU: the unit cases, then a session on the mock display.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT"
mkdir -p build/tests
tcc -Wall tools/tests/gamescope_bridge_gate.c -o build/tests/gamescope_bridge_gate
build/tests/gamescope_bridge_gate unit
. tools/tests/gate_display.sh
gate_display_setup
trap gate_display_cleanup EXIT
env $GATE_DISPLAY_ENV python3 tools/tests/gamescope_bridge_gate.py build/tests/gamescope_bridge_gate
