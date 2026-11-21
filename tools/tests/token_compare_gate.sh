#!/bin/sh
# Security review SR-11 (Gitea #193): secrets are compared in constant time.
# The unit cases pin blit_token_equal; the source check fails on any strcmp,
# strncmp or memcmp whose arguments name a token or the Authorization header
# (the bearer token was compared as `strcmp(authorization, expected)`).
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
probe="$root/build/tests/token_compare_gate"
mkdir -p "$root/build/tests"
tcc -std=c99 -O2 -I"$root/src" "$root/tools/tests/token_compare_gate.c" -o "$probe"
"$probe"
found=$(grep -niE '(str|strn|mem)cmp\([^()]*(token|authorization)[^()]*\)' \
    "$root"/src/*.c "$root"/src/*.h || true)
if [ -n "$found" ]; then
    printf 'TOKEN_COMPARE_FAIL a token compared with strcmp, strncmp or memcmp:\n%s\n' "$found" >&2
    exit 1
fi
echo 'TOKEN_COMPARE_PASS source=no-strcmp-on-tokens'
