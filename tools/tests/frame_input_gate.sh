#!/bin/sh
# ADR 0228: the frame channel's INPUT requests and their routes to
# blit-compositor's input sockets: the answers and the states (local), then
# every input case read off the socket, several routes included (sock).
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT"
. tools/tests/gate_display.sh
gate_display_setup
trap gate_display_cleanup EXIT
env $GATE_DISPLAY_ENV python3 tools/tests/frame_input_gate.py local
env $GATE_DISPLAY_ENV python3 tools/tests/frame_input_gate.py sock
