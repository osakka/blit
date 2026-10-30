#!/bin/sh
# ADR 0202: frames by descriptor, on the mock display.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT"
. tools/tests/gate_display.sh
gate_display_setup
trap gate_display_cleanup EXIT
env $GATE_DISPLAY_ENV python3 tools/tests/frame_descriptor_gate.py
