#!/bin/sh
# ADR 0223: frames by dmabuf, the parts that need no GPU import, on the mock display.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT"
. tools/tests/gate_display.sh
gate_display_setup
trap gate_display_cleanup EXIT
BLIT_GATE_SERVER_ENV="$GATE_DISPLAY_ENV ${BLIT_GATE_SERVER_ENV:-}" python3 tools/tests/frame_dmabuf_gate.py
