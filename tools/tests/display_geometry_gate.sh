#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
probe="$root/build/tests/display_geometry_gate"
mkdir -p "$root/build/tests"
tcc -std=c99 -O2 "$root/tools/tests/display_geometry_gate.c" -lm -o "$probe"
"$probe"
