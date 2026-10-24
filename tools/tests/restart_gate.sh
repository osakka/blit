#!/bin/sh
set -eu
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/gate_port.sh"
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/gate_display.sh"
gate_display_setup

binary=${BLIT_GATE_BINARY:-build/blit/blit-server}
port=${BLIT_RESTART_PORT:-19198}
port=$(gate_port "$port")
log=${TMPDIR:-/tmp}/blit-restart-gate.$$.log
pid=

cleanup()
{
    gate_display_cleanup
    if [ -n "${pid}" ] && kill -0 "${pid}" 2>/dev/null; then
        kill -TERM "${pid}" 2>/dev/null || true
        wait "${pid}" 2>/dev/null || true
    fi
    rm -f "$log"
}
trap cleanup EXIT INT TERM

env $GATE_DISPLAY_ENV LD_LIBRARY_PATH=build/sdl3-kms \
    "$binary" "$port" >"$log" 2>&1 &
pid=$!
ready=0
for attempt in 1 2 3 4 5 6 7 8 9 10; do
    if curl --connect-timeout 1 --max-time 2 --fail --silent \
        "http://127.0.0.1:$port/v1/version" >/dev/null 2>&1; then
        ready=1
        break
    fi
    sleep 0.2
done
test "$ready" = 1

response=$(curl --connect-timeout 2 --max-time 3 --fail --silent --show-error \
    --request POST "http://127.0.0.1:$port/v1/control/restart")
printf '%s\n' "$response" | grep -q '"state":"restarting"'

restarted=0
for attempt in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15; do
    if curl --connect-timeout 1 --max-time 2 --fail --silent \
        "http://127.0.0.1:$port/v1/version" >/dev/null 2>&1; then
        restarted=1
        break
    fi
    sleep 0.2
done
test "$restarted" = 1
runtime=$(curl --connect-timeout 2 --max-time 3 --fail --silent \
    "http://127.0.0.1:$port/v1/diagnostics/runtime")
printf '%s\n' "$runtime" | grep -q '"ready":true'
printf 'RESTART_GATE_PASS same_binary=1 port=%s\n' "$port"
