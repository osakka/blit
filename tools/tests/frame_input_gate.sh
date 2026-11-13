#!/bin/sh
# ADR 0228: the frame channel's INPUT requests and their routes to
# blit-compositor's input sockets: the answers and the states (local), then
# every input case read off the socket, several routes included (sock), and
# one window routed from two displays' sessions (displays).
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT"
. tools/tests/gate_display.sh
gate_display_setup
trap gate_display_cleanup EXIT
BLIT_GATE_SERVER_ENV="$GATE_DISPLAY_ENV ${BLIT_GATE_SERVER_ENV:-}" python3 tools/tests/frame_input_gate.py local
BLIT_GATE_SERVER_ENV="$GATE_DISPLAY_ENV ${BLIT_GATE_SERVER_ENV:-}" python3 tools/tests/frame_input_gate.py sock
# The pointer's session asked once an event, however many routes (0.3.83's
# regression made dragging clunky): counted, not timed.
BLIT_GATE_SERVER_ENV="$GATE_DISPLAY_ENV ${BLIT_GATE_SERVER_ENV:-}" python3 tools/tests/frame_input_gate.py scans
# Two displays, a session on each: the pointer's route is the session
# under it, the keys' the focused one (docs/api.md).
printf '2 0 0 0\n1 1024 768\n2 1024 768\n' > "$GATE_DISPLAY_DIR/state"
BLIT_GATE_SERVER_ENV="$GATE_DISPLAY_ENV ${BLIT_GATE_SERVER_ENV:-}" python3 tools/tests/frame_input_gate.py displays
# The same two displays: their refresh rates on the channel, for a desktop
# the login manager started (`DISPLAY rate`).
BLIT_GATE_SERVER_ENV="$GATE_DISPLAY_ENV ${BLIT_GATE_SERVER_ENV:-}" python3 tools/tests/frame_input_gate.py rates
