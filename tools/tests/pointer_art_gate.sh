#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
bin=$(mktemp "${TMPDIR:-/tmp}/blit-pointer-art.XXXXXX")
trap 'rm -f "$bin"' EXIT HUP INT TERM
tcc -O2 -std=c99 "$root/tools/tests/pointer_art_gate.c" -o "$bin"
"$bin"
