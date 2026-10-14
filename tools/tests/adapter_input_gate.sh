#!/bin/sh
# Issues #76 and #77. Compile the shipped Hibr adapter into a harness and
# assert its pointer-coordinate and key-name translations.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
PROBE=$ROOT/build/tests/adapter_input_gate
hibr_root=${BLIT_HIBR_ROOT:-}

if [ -z "$hibr_root" ] || [ ! -d "$hibr_root/include" ] ||
   [ ! -d "$hibr_root/mods" ]; then
    echo "adapter-input gate: BLIT_HIBR_ROOT is not a Hibr checkout"
    exit 77
fi

mkdir -p "$ROOT/build/tests"
gcc -std=gnu99 -O1 -Wall -Wextra -Wno-format-truncation \
    -I"$hibr_root/include" -I"$hibr_root/mods" -I"$ROOT/hibr" \
    -o "$PROBE" "$ROOT/tools/tests/adapter_input_gate.c"
"$PROBE"
