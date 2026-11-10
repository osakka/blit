#!/bin/sh
# ADR 0225: blit-gamescope's frame and terminal cores, without gamescope,
# PipeWire or a GPU: the unit cases, then a session on the mock display.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT"
# The bridge never captures the mouse and turns on no mouse reporting: no
# ?7701h, no mode 1000 or 1006 anywhere in it (capture is the desktop's).
if grep -n '7701h\|?1000h\|?1006h\|requested_size' src/blit_gamescope.c src/blit_gamescope_tty.h \
        src/blit_gamescope_frames.h; then
    echo "GAMESCOPE_BRIDGE_FAIL the bridge captures, asks for mouse reports or resizes its stream" >&2
    exit 1
fi
# --no-place: the bridge writes Kitty placements and deletes only through
# GsPlacement (which writes neither with --no-place), never directly.
if grep -n 'gs_frames_place\|gs_frames_delete\|_Ga=' src/blit_gamescope.c; then
    echo "GAMESCOPE_BRIDGE_FAIL the bridge writes a placement or delete around GsPlacement" >&2
    exit 1
fi
# Phase 2: under --no-place the desktop app sends INPUT on/off, so main
# hands --no-place to the frames state (the unit case checks the effect).
grep -q 'frames.focus_by_desktop = b->placement.no_place;' src/blit_gamescope.c || {
    echo "GAMESCOPE_BRIDGE_FAIL --no-place does not leave INPUT on/off to the desktop" >&2
    exit 1
}
# Version 5 of the frame channel only (docs/api.md, "One grammar, version
# 5"): none of the version 4 line forms is read or written by the bridge.
if grep -nE '"(RELEASE|ERROR|OK input|OK window)|INPUT eis %s|version = [1-4],' \
        src/blit_gamescope.c src/blit_gamescope_frames.h; then
    echo "GAMESCOPE_BRIDGE_FAIL the bridge still speaks a version 4 line form" >&2
    exit 1
fi
mkdir -p build/tests
tcc -Wall tools/tests/gamescope_bridge_gate.c -o build/tests/gamescope_bridge_gate
build/tests/gamescope_bridge_gate unit
. tools/tests/gate_display.sh
gate_display_setup
trap gate_display_cleanup EXIT
env $GATE_DISPLAY_ENV python3 tools/tests/gamescope_bridge_gate.py build/tests/gamescope_bridge_gate
