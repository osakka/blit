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

env $GATE_SERVER_ENV $GATE_DISPLAY_ENV \
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

# #155: the program writes nothing, and it is running all the same, from
# the creation's answer on (Blit up to 0.3.98 said `starting` until a
# first byte, which a client polling for an exit took as not yet started).
state() {
    curl --connect-timeout 2 --max-time 3 --fail --silent --show-error \
        "http://127.0.0.1:$port/v1/terminal-sessions/$1" |
        sed -n 's/.*"state":"\([a-z]*\)".*/\1/p'
}
grep -q '"state":"running"' "$session" ||
    { echo "SESSION_LIFECYCLE_FAIL created silent session: $(cat "$session")"; exit 1; }
sleep 1.5
silent=$(state 1)
[ "$silent" = running ] ||
    { echo "SESSION_LIFECYCLE_FAIL a silent program 1.5 s in: $silent"; exit 1; }

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

# A silent program that ends: running, then stopped (exit 0).
curl --fail --silent --show-error --request POST \
    --header 'Content-Type: application/json' \
    --data '{"command":"/bin/sh","arguments":["-c","sleep 1"],"surface":{"width":80,"height":24}}' \
    "http://127.0.0.1:$port/v1/terminal-sessions" >"$session"
second=$(sed -n 's/.*"id":"\{0,1\}\([0-9][0-9]*\).*/\1/p' "$session" | head -n 1)
[ "$(state "$second")" = running ] ||
    { echo "SESSION_LIFECYCLE_FAIL the silent sleep: $(state "$second")"; exit 1; }
i=0
while [ "$(state "$second")" = running ] && [ "$i" -lt 50 ]; do i=$((i + 1)); sleep 0.1; done
ended=$(state "$second")
[ "$ended" = stopped ] ||
    { echo "SESSION_LIFECYCLE_FAIL the silent sleep ended as $ended"; exit 1; }
printf 'SESSION_LIFECYCLE_PASS delete_ms=%s diagnostics=responsive silent=running,then-stopped\n' "$elapsed_ms"
