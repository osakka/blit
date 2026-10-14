#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
HIBR_ROOT=${BLIT_HIBR_ROOT:-}

if [ -z "$HIBR_ROOT" ] || [ ! -f "$HIBR_ROOT/include/hibr.h" ]; then
	echo "adapter-provider gate: BLIT_HIBR_ROOT is required" >&2
	exit 77
fi

out=${TMPDIR:-/tmp}/blit-adapter-provider-$$
trap 'rm -f "$out"' EXIT HUP INT TERM

tcc -O2 -std=c99 -Wall -Wextra \
	-I"$HIBR_ROOT/include" -I"$HIBR_ROOT/mods" \
	-I"$ROOT/hibr" -I"$ROOT" \
	-o "$out" "$ROOT/tools/tests/adapter_provider_gate.c"

"$out"
