#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
mkdir -p "$root/build/tests"
bin="$root/build/tests/surface_damage_gate"
cflags=$("$root/tools/build-flags.sh" --cflags server)
libs=$("$root/tools/build-flags.sh" --libs server)
# shellcheck disable=SC2086
tcc -O2 -std=c99 $cflags \
    "$root/tools/tests/surface_damage_gate.c" \
    $("$root/tools/build-flags.sh" --sources server) \
    $libs \
    -o "$bin"
"$bin"
