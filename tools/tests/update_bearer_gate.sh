#!/bin/sh
# Security review SR-2 (Gitea #184): with an API token set, the update
# route takes the update token AND the bearer token; the update token
# alone, sent in clear, is refused. Without an API token (loopback only)
# the update token suffices, as before.
set -eu
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/gate_port.sh"
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/gate_display.sh"
gate_display_setup

binary=${BLIT_GATE_BINARY:-build/blit/blit-server}
port=$(gate_port "${BLIT_UPDATE_BEARER_PORT:-19276}")
dir=$(mktemp -d "${TMPDIR:-/tmp}/blit-update-bearer.XXXXXX")
pid=
cleanup()
{
    gate_display_cleanup
    if [ -n "$pid" ]; then kill -TERM "$pid" 2>/dev/null || true; wait "$pid" 2>/dev/null || true; fi
    rm -rf "$dir"
}
trap cleanup EXIT INT TERM
fail() { echo "UPDATE_BEARER_FAIL $*"; exit 1; }
refused() { [ "$1" = 401 ] || [ "$1" = 403 ]; }

start()   # start [api-token]
{
    printf '%s\n' update-secret >"$dir/update"
    env $GATE_SERVER_ENV $GATE_DISPLAY_ENV \
        BLIT_UPDATE_TOKEN_FILE="$dir/update" BLIT_UPDATE_DIRECTORY="$dir" \
        ${1:+BLIT_AUTH_TOKEN=$1} "$binary" "$port" >"$dir/log" 2>&1 &
    pid=$!
    i=0
    until curl -s -o /dev/null --max-time 2 "http://127.0.0.1:$port/v1/server"; do
        i=$((i + 1)); [ "$i" -lt 20 ] || fail "server did not start"; sleep 0.5
    done
}
stop() { kill -TERM "$pid" 2>/dev/null || true; wait "$pid" 2>/dev/null || true; pid=; }

# POST /v1/control/update/cancel goes through the API's own bearer
# check (401) and then the update check (403); the upload (PUT) is read
# before the API's routing, so the update check alone guards it. Refused
# is 401 or 403; anything else is past the checks.
cancel() { curl -s -o /dev/null -w "%{http_code}" --max-time 5 -X POST "$@" \
    "http://127.0.0.1:$port/v1/control/update/cancel"; }

start api-secret
a=$(cancel -H "X-Blit-Update-Token: update-secret")
b=$(cancel -H "Authorization: Bearer api-secret")
c=$(cancel -H "X-Blit-Update-Token: update-secret" -H "Authorization: Bearer wrong")
d=$(cancel -H "X-Blit-Update-Token: update-secret" -H "Authorization: Bearer api-secret")
e=$(curl -s -o /dev/null -w '%{http_code}' --max-time 5 -X PUT \
    -H "X-Blit-Update-Token: update-secret" -H "Content-Type: application/vnd.debian.binary-package" \
    -H "X-Blit-Packages: 4:$(printf '%064d' 0)" --data-binary 'abcd' \
    "http://127.0.0.1:$port/v1/control/update")
stop
refused "$a" || fail "update token alone was accepted ($a)"
refused "$b" || fail "bearer alone was accepted ($b)"
refused "$c" || fail "a wrong bearer was accepted ($c)"
! refused "$d" || fail "both tokens were refused ($d)"
refused "$e" || fail "an upload with the update token alone was not refused ($e)"

start
f=$(cancel -H "X-Blit-Update-Token: update-secret")
stop
! refused "$f" || fail "with no API token the update token alone was refused ($f)"
echo "UPDATE_BEARER_PASS update_only=$a bearer_only=$b wrong_bearer=$c both=$d upload_update_only=$e tokenless_update_only=$f"
