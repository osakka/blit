#!/bin/sh
# Blit's login wrapper hands the login screen the seat and VT this display
# is (hibr #367): a login screen running on Blit's pty can see neither, and
# logind then registered every such login as a remote session with no seat,
# so the person at the machine had no Bluetooth audio, no network without
# root's password and no brightness. With BLIT_VTNR set the wrapper gives
# seat0 and that VT; without it, nothing, and a login is as it was. The
# login screen itself is a stub that prints what it was handed.
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
hibr=${BLIT_GATE_HIBR:-/usr/bin/hibr}
[ -x "$hibr" ] || { printf 'LOGIN_SEAT_GATE_SKIP hibr=missing\n'; exit 77; }
dir=$(mktemp -d "${TMPDIR:-/tmp}/blit-login-seat.XXXXXX")
trap 'rm -rf "$dir"' EXIT INT TERM
printf 'lg_main() { echo "seat=$LG_SEAT vt=$LG_VTNR"; }\n' > "$dir/stub.hibr"

with=$(BLIT_VTNR=7 BLIT_HIBR_LOGIN_SCRIPT="$dir/stub.hibr" \
    "$hibr" "$root/hibr/blit-login.hibr")
without=$(env -u BLIT_VTNR BLIT_HIBR_LOGIN_SCRIPT="$dir/stub.hibr" \
    "$hibr" "$root/hibr/blit-login.hibr")
if [ "$with" != "seat=seat0 vt=7" ]; then
    printf 'LOGIN_SEAT_GATE_FAIL with_vt=%s\n' "$with" >&2
    exit 1
fi
if [ "$without" != "seat= vt=" ]; then
    printf 'LOGIN_SEAT_GATE_FAIL without_vt=%s\n' "$without" >&2
    exit 1
fi
printf 'LOGIN_SEAT_GATE_PASS with_vt=seat0/7 without_vt=unset\n'
