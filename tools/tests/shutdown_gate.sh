#!/bin/sh
set -eu
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/gate_port.sh"
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/gate_display.sh"
gate_display_setup

binary=${BLIT_GATE_BINARY:-build/blit/blit-server}
port=${BLIT_SHUTDOWN_PORT:-19197}
port=$(gate_port "$port")
log=${TMPDIR:-/tmp}/blit-shutdown-gate.$$.log
pid=

cleanup()
{
    gate_display_cleanup
    if [ -n "${pid}" ] && kill -0 "$pid" 2>/dev/null; then
        kill -KILL "$pid" 2>/dev/null || true
        wait "$pid" 2>/dev/null || true
    fi
    rm -f "$log" "$log.body"
}
trap cleanup EXIT INT TERM

# The control route is guarded like every route (Gitea #176): a request
# without the token, or with a wrong one, is refused and stops nothing.
token=$(od -An -N16 -tx1 /dev/urandom | tr -d ' \n')
env $GATE_SERVER_ENV $GATE_DISPLAY_ENV BLIT_AUTH_TOKEN="$token" \
    "$binary" "$port" >"$log" 2>&1 &
pid=$!
ready=0
for attempt in 1 2 3 4 5; do
    if curl --connect-timeout 1 --max-time 2 --fail --silent \
        -H "Authorization: Bearer $token" \
        "http://127.0.0.1:$port/v1/server" >/dev/null 2>&1; then
        ready=1
        break
    fi
    sleep 1
done
test "$ready" = 1

for auth in none wrong; do
    case $auth in
        none) code=$(curl --connect-timeout 2 --max-time 3 --silent --output /dev/null \
                  --write-out '%{http_code}' --request POST \
                  "http://127.0.0.1:$port/v1/control/shutdown") ;;
        wrong) code=$(curl --connect-timeout 2 --max-time 3 --silent --output /dev/null \
                  --write-out '%{http_code}' --request POST -H "Authorization: Bearer not-$token" \
                  "http://127.0.0.1:$port/v1/control/shutdown") ;;
    esac
    test "$code" = 401 || { echo "SHUTDOWN_GATE_FAIL a shutdown with $auth token answered $code, not 401" >&2; exit 1; }
    sleep 0.3
    kill -0 "$pid" 2>/dev/null || { echo "SHUTDOWN_GATE_FAIL a shutdown with $auth token stopped the server" >&2; exit 1; }
done
code=$(curl --connect-timeout 2 --max-time 3 --silent --show-error --output "$log.body" \
    --write-out '%{http_code}' --request POST -H "Authorization: Bearer $token" \
    "http://127.0.0.1:$port/v1/control/shutdown")
test "$code" = 202 || { echo "SHUTDOWN_GATE_FAIL the shutdown answered $code, not 202" >&2; exit 1; }
grep -q '"state":"stopping"' "$log.body"
exited=0
for attempt in 1 2 3 4 5; do
    if kill -0 "$pid" 2>/dev/null; then
        sleep 1
    else
        exited=1
        break
    fi
done
test "$exited" = 1
wait "$pid" 2>/dev/null || true
pid=
printf 'SHUTDOWN_GATE_PASS process_exited=1 no_token=401 wrong_token=401 token=202\n'
