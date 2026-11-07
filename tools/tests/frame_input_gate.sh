#!/bin/sh
# ADR 0225: the frame channel's INPUT request, the cases that need no libeis:
# ENOTSUP without libei, and against a stand-in libei the EINVAL, ENOENT,
# EACCES, OK input and EBUSY answers and the route's states. What reaches a
# compositor is frame_input_eis_gate.sh.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT"
mkdir -p build/tests/libei-stub
tcc -shared -fPIC -Wall tools/tests/fixtures/libei_stub.c -o build/tests/libei-stub/libei.so.1
. tools/tests/gate_display.sh
gate_display_setup
trap gate_display_cleanup EXIT
env $GATE_DISPLAY_ENV python3 tools/tests/frame_input_gate.py local
env $GATE_DISPLAY_ENV python3 tools/tests/frame_input_gate.py stub
