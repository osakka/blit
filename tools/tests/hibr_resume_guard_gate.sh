#!/bin/sh
set -eu
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/gate_port.sh"

root=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
port=${BLIT_RESUME_GUARD_PORT:-19214}
port=$(gate_port "$port")
log=$(mktemp "${TMPDIR:-/tmp}/blit-resume-guard.XXXXXX")
server_pid=

cleanup() {
    if [ -n "$server_pid" ]; then
        kill "$server_pid" 2>/dev/null || true
        wait "$server_pid" 2>/dev/null || true
    fi
    rm -f "$log"
}
trap cleanup EXIT INT TERM

SDL_VIDEODRIVER=dummy LD_LIBRARY_PATH="$root/build/sdl3-kms" \
    "$root/build/blit/blit-server" "$port" >"$log" 2>&1 &
server_pid=$!

ready=0
i=0
while [ "$i" -lt 50 ]; do
    if curl -fsS --max-time 1 "http://127.0.0.1:$port/v1/version" >/dev/null 2>&1; then
        ready=1
        break
    fi
    i=$((i + 1))
    sleep 0.1
done
[ "$ready" -eq 1 ] || { echo "RESUME_GUARD_FAIL server_not_ready"; exit 1; }

response=$(curl -fsS --max-time 5 -H 'Content-Type: application/json' \
    --data '{"command":"'"$root"'/tools/tests/fixtures/desktop","arguments":["--resume"],"surface":{"width":80,"height":24}}' \
    "http://127.0.0.1:$port/v1/terminal-sessions")
session_id=$(printf '%s' "$response" | sed -n 's/.*"id":"\([0-9][0-9]*\)".*/\1/p')
[ -n "$session_id" ] || { echo "RESUME_GUARD_FAIL no_session_id"; exit 1; }

i=0
while [ "$i" -lt 60 ]; do
    state=$(curl -fsS --max-time 2 \
        "http://127.0.0.1:$port/v1/terminal-sessions/$session_id")
    case "$state" in
        *'"state":"failed"'*'"unresolved":true'*)
            printf '%s\n' "RESUME_GUARD_PASS marker=hibr_attach_unresolved"
            exit 0
            ;;
    esac
    i=$((i + 1))
    sleep 0.25
done

echo "RESUME_GUARD_FAIL response=$state"
exit 1
