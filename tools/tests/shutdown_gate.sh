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
    rm -f "$log"
}
trap cleanup EXIT INT TERM

env $GATE_DISPLAY_ENV \
    "$binary" "$port" >"$log" 2>&1 &
pid=$!
ready=0
for attempt in 1 2 3 4 5; do
    if curl --connect-timeout 1 --max-time 2 --fail --silent \
        "http://127.0.0.1:$port/v1/server" >/dev/null 2>&1; then
        ready=1
        break
    fi
    sleep 1
done
test "$ready" = 1

response=$(curl --connect-timeout 2 --max-time 3 --fail --silent --show-error \
    --request POST "http://127.0.0.1:$port/v1/control/shutdown")
printf '%s\n' "$response" | grep -q '\"state\":\"stopping\"'
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
printf 'SHUTDOWN_GATE_PASS process_exited=1\n'
