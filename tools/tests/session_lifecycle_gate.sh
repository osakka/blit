#!/bin/sh
set -eu
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/gate_port.sh"
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/gate_display.sh"
gate_display_setup

binary=${BLIT_GATE_BINARY:-build/blit/blit-server}
port=${BLIT_LIFECYCLE_PORT:-19200}
port=$(gate_port "$port")
log=${TMPDIR:-/tmp}/blit-lifecycle-gate.$$.log
session=${TMPDIR:-/tmp}/blit-lifecycle-gate.$$.session
delete_body=${TMPDIR:-/tmp}/blit-lifecycle-gate.$$.delete
runtime=${TMPDIR:-/tmp}/blit-lifecycle-gate.$$.runtime
pid=

cleanup()
{
    gate_display_cleanup
    if [ -n "${pid}" ]; then
        kill -TERM "${pid}" 2>/dev/null || true
        wait "${pid}" 2>/dev/null || true
    fi
    rm -f "$log" "$session" "$delete_body" "$runtime"
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

# This child intentionally ignores SIGTERM. API deletion must escalate to
# SIGKILL without blocking the gateway indefinitely.
curl --fail --silent --show-error --request POST \
    --header 'Content-Type: application/json' \
    --data '{"command":"/bin/sh","arguments":["-c","trap : TERM; while :; do sleep 1; done"],"surface":{"width":80,"height":24}}' \
    "http://127.0.0.1:$port/v1/terminal-sessions" >"$session"

start=$(date +%s%N)
curl --connect-timeout 2 --max-time 3 --fail --silent --show-error \
    --request DELETE "http://127.0.0.1:$port/v1/terminal-sessions/1" \
    >"$delete_body"
end=$(date +%s%N)
elapsed_ms=$(( (end - start) / 1000000 ))

curl --connect-timeout 2 --max-time 3 --fail --silent --show-error \
    "http://127.0.0.1:$port/v1/diagnostics/runtime" >"$runtime"
test "$elapsed_ms" -lt 2000
grep -q '"sessions":0' "$runtime"
printf 'SESSION_LIFECYCLE_PASS delete_ms=%s diagnostics=responsive\n' "$elapsed_ms"
