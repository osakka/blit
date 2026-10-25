#!/bin/sh
set -eu
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/gate_port.sh"
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/gate_display.sh"
gate_display_setup

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
binary=${BLIT_GATE_BINARY:-$root/build/blit/blit-server}
port=${BLIT_LOGIN_PORT:-19210}
port=$(gate_port "$port")
token=blit-login-gate
log=${TMPDIR:-/tmp}/blit-login-gate.$$.log
pid=

if ! sudo -n true >/dev/null 2>&1; then
    printf 'LOGIN_MANAGER_GATE_SKIP root_service=required\n'
    exit 77
fi

cleanup()
{
    gate_display_cleanup
    if [ -n "${pid}" ] && kill -0 "${pid}" 2>/dev/null; then
        sudo -n kill -TERM "${pid}" 2>/dev/null || true
        wait "${pid}" 2>/dev/null || true
    fi
    rm -f "$log"
}
trap cleanup EXIT INT TERM

sudo -n env $GATE_DISPLAY_ENV \
    BLIT_LOGIN_COMMAND=/bin/true BLIT_LOGIN_SCRIPT='' \
    BLIT_LOGIN_AUTOSTART=0 BLIT_AUTH_TOKEN="$token" \
    "$binary" "$port" >"$log" 2>&1 &
pid=$!
ready=0
for attempt in 1 2 3 4 5 6 7 8 9 10; do
    if curl --connect-timeout 1 --max-time 2 --fail --silent \
        -H "Authorization: Bearer $token" \
        "http://127.0.0.1:$port/v1/version" >/dev/null 2>&1; then
        ready=1
        break
    fi
    sleep .2
done
test "$ready" = 1

first=$(curl --fail --silent --show-error \
    -H "Authorization: Bearer $token" -H 'Content-Type: application/json' \
    --data '{}' "http://127.0.0.1:$port/v1/login")
printf '%s\n' "$first" | grep -q '"enabled":true'
printf '%s\n' "$first" | grep -q '"login_manager":true'
first_id=$(printf '%s\n' "$first" | sed -n 's/.*"session":{"id":"\([0-9][0-9]*\)".*/\1/p')
test -n "$first_id"

# /bin/true exits immediately. The manager must not leave a dead login screen
# as a successful running resource; it starts a fresh supervised session.
second_id=
for attempt in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15; do
    state=$(curl --fail --silent -H "Authorization: Bearer $token" \
        "http://127.0.0.1:$port/v1/login")
    second_id=$(printf '%s\n' "$state" | sed -n 's/.*"session":{"id":"\([0-9][0-9]*\)".*/\1/p')
    if [ -n "$second_id" ] && [ "$second_id" != "$first_id" ]; then
        break
    fi
    sleep .2
done
test -n "$second_id"
test "$second_id" != "$first_id"
runtime=$(curl --fail --silent --show-error \
    -H "Authorization: Bearer $token" \
    "http://127.0.0.1:$port/v1/diagnostics/runtime")
printf '%s\n' "$runtime" | grep -q 'login_session_exit'

code=$(curl --silent --show-error -o /dev/null -w '%{http_code}' \
    -H "Authorization: Bearer $token" -X DELETE \
    "http://127.0.0.1:$port/v1/login")
test "$code" = 204
printf 'LOGIN_MANAGER_GATE_PASS restart=%s->%s stop_status=%s\n' \
    "$first_id" "$second_id" "$code"
