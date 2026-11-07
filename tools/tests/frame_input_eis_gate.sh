#!/bin/sh
# ADR 0225: what Blit sends a compositor's EIS socket, against a stand-in EIS
# server (tools/tests/eis_receiver.c, libeis.so.1) and the system's libei.so.1.
# Skips (77) on a host without either; on Debian 13: apt install libei1 libeis1.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT"
mkdir -p build/tests
tcc -Wall tools/tests/eis_receiver.c -o build/tests/eis_receiver
if ! build/tests/eis_receiver probe; then
    echo "FRAME_INPUT_EIS_SKIP libeis.so.1 is not installed (Debian: libeis1)"
    exit 77
fi
if ! python3 -c 'import ctypes; ctypes.CDLL("libei.so.1")' 2>/dev/null; then
    echo "FRAME_INPUT_EIS_SKIP libei.so.1 is not installed (Debian: libei1)"
    exit 77
fi
. tools/tests/gate_display.sh
gate_display_setup
trap gate_display_cleanup EXIT
env $GATE_DISPLAY_ENV python3 tools/tests/frame_input_gate.py eis
