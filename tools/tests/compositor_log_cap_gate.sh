#!/bin/sh
# Gitea #156: blit-compositor's log is capped. A log past 16 MiB when it is
# opened becomes <path>.1 (replacing the one before) and a new one starts
# with a line saying where the old lines are; a log under the cap is
# appended to. (Past the cap while running, the same rotation follows a line.)
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
BIN=${BLIT_COMPOSITOR_BINARY:-$ROOT/build/blit/blit-compositor}
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
fail() { echo "COMPOSITOR_LOG_CAP_FAIL $*"; exit 1; }
# Under the cap: appended to, nothing rotated.
printf 'earlier line\n' > "$T/c.log"
env -i PATH=/usr/bin:/bin "$BIN" --log "$T/c.log" > /dev/null 2>&1 || true
[ ! -e "$T/c.log.1" ] || fail "a small log was rotated"
grep -q '^earlier line$' "$T/c.log" && grep -q ' version ' "$T/c.log" || fail "a small log was not appended to"
# Over the cap: rotated, the old one kept once as .1.
truncate -s 17M "$T/c.log"
printf 'older\n' > "$T/c.log.1"
env -i PATH=/usr/bin:/bin "$BIN" --log "$T/c.log" > /dev/null 2>&1 || true
[ "$(stat -c %s "$T/c.log.1")" = 17825792 ] || fail "the old log is not .1: $(stat -c %s "$T/c.log.1" 2>&1)"
size=$(stat -c %s "$T/c.log")
[ "$size" -lt 4096 ] || fail "the new log is $size bytes"
grep -q "the 17825792 bytes before are in $T/c.log.1" "$T/c.log" || fail "the new log does not say where the old lines are"
grep -q ' version ' "$T/c.log" || fail "the new log has no lines"
echo "COMPOSITOR_LOG_CAP_PASS small=appended over_16MiB=rotated_to_.1 new_log=${size}B"
