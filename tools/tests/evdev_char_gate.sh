#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
probe="$root/build/tests/evdev_char_gate"
mkdir -p "$root/build/tests"
tcc -std=c99 -O2 "$root/tools/tests/evdev_char_gate.c" -o "$probe"
"$probe"
