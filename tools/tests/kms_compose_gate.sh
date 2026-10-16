#!/bin/sh
# Builds and runs the KMS compositor pixel gate (tools/tests/kms_compose_gate.c).
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
bin=${TMPDIR:-/tmp}/blit-kms-compose-$$
trap 'rm -f "$bin"' EXIT
tcc -O2 -std=c99 \
    -I"$ROOT/vendor/SDL3/include" \
    -I"$ROOT/build/deps/prefix/include" \
    -I"$ROOT/build/deps/prefix/include/libdrm" \
    "$ROOT/tools/tests/kms_compose_gate.c" "$ROOT/src/blit_font.c" \
    -L"$ROOT/build/deps/prefix/lib/x86_64-linux-gnu" -ldrm -lz -o "$bin"
LD_LIBRARY_PATH="$ROOT/build/deps/prefix/lib/x86_64-linux-gnu" "$bin"
