#!/bin/sh
# ADR 0234: the shake detector (src/blit_pointer_shake.h) with synthetic
# positions and times: built by tcc as Blit is, then under AddressSanitizer
# and UndefinedBehaviorSanitizer where the host's cc has them.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
out=$(mktemp -d "${TMPDIR:-/tmp}/blit-pointer-shake.XXXXXX")
trap 'rm -rf "$out"' EXIT
tcc -std=c99 -Wall -I"$ROOT/src" -o "$out/gate" "$ROOT/tools/tests/pointer_shake_gate.c" -lm
"$out/gate"
san="-O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all"
if ${CC:-cc} $san -x c -o "$out/probe" - >/dev/null 2>&1 <<'PROBE'
int main(void) { return 0; }
PROBE
then
    # shellcheck disable=SC2086
    ${CC:-cc} -std=c99 -Wall -Wextra $san -I"$ROOT/src" -o "$out/gate-san" \
        "$ROOT/tools/tests/pointer_shake_gate.c" -lm
    "$out/gate-san" >/dev/null
    echo "POINTER_SHAKE_SANITIZERS_OK address,undefined"
fi
