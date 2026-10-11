#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
probe="$root/build/tests/audio_control_gate"
mkdir -p "$root/build/tests"
tcc -std=c99 -O2 "$root/tools/tests/audio_control_gate.c" -o "$probe"
"$probe"
