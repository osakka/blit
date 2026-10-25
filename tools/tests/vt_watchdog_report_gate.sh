#!/bin/sh
# blit #146: the console watchdog's report names the stuck step and threads.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
PROBE=$ROOT/build/tests/vt_watchdog_report_gate
mkdir -p "$ROOT/build/tests"
tcc -std=c99 -o "$PROBE" "$ROOT/tools/tests/vt_watchdog_report_gate.c" -lpthread
"$PROBE"
