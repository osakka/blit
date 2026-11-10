#!/bin/sh
# Builds and runs the KMS compositor pixel gate (tools/tests/kms_compose_gate.c).
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
bin=${TMPDIR:-/tmp}/blit-kms-compose-$$
trap 'rm -f "$bin"' EXIT
cflags=$("$ROOT/tools/build-flags.sh" --cflags drm zlib)
libs=$("$ROOT/tools/build-flags.sh" --libs drm zlib)
# shellcheck disable=SC2086
tcc -O2 -std=c99 $cflags \
    "$ROOT/tools/tests/kms_compose_gate.c" "$ROOT/src/blit_font.c" \
    $libs -o "$bin"
"$bin"
