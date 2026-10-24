#!/bin/sh
# Issue #30. Build the Hibr display provider into a harness and assert that a
# frame which changes nothing costs nothing. See adapter_coalesce_gate.c.
set -eu
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/gate_port.sh"
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/gate_display.sh"
gate_display_setup

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
BINARY=${BLIT_GATE_BINARY:-$ROOT/build/blit/blit-server}
PORT=${BLIT_GATE_PORT:-19246}
PORT=$(gate_port "$PORT")
FONT=${BLIT_GATE_FONT:-$ROOT/assets/fonts/Blit-Hibr-Terminus16.psf}
PROBE=$ROOT/build/tests/adapter_coalesce_gate
LOG=$(mktemp /tmp/blit-coalesce-gate.XXXXXX)
SESSION=$(mktemp /tmp/blit-coalesce-session.XXXXXX)
server_pid=
cleanup() {
    gate_display_cleanup
    if [ -n "$server_pid" ]; then kill "$server_pid" 2>/dev/null || true; fi
    rm -f "$LOG" "$SESSION"
}
trap cleanup EXIT HUP INT TERM

# The adapter is compiled against the Hibr headers, so without them this gate
# cannot run at all. 77 is the suite's not-applicable status, so the skip is
# reported rather than counted as a pass.
hibr_root=${BLIT_HIBR_ROOT:-}
if [ -z "$hibr_root" ] || [ ! -d "$hibr_root/include" ] ||
   [ ! -d "$hibr_root/mods" ]; then
    echo "adapter-coalesce gate: BLIT_HIBR_ROOT is not a Hibr checkout"
    exit 77
fi
if [ ! -x "$BINARY" ]; then
    echo "adapter-coalesce gate: missing executable $BINARY" >&2
    exit 1
fi

mkdir -p "$ROOT/build/tests"
# gcc rather than tcc, and warnings on: this compiles the shipped adapter, so
# the build is also the only place its warnings are visible. The one
# suppression is a false positive: gcc inlines cb_register_owner, loses the
# 64-byte bound on cb.session, and assumes the 256-byte path could overflow.
gcc -std=gnu99 -O1 -Wall -Wextra -Wno-format-truncation \
    -I"$hibr_root/include" -I"$hibr_root/mods" -I"$ROOT/hibr" \
    -o "$PROBE" "$ROOT/tools/tests/adapter_coalesce_gate.c"

env $GATE_DISPLAY_ENV LD_LIBRARY_PATH="$ROOT/build/sdl3-kms" \
    BLIT_PSF_FONT="$FONT" "$BINARY" "$PORT" >"$LOG" 2>&1 &
server_pid=$!
ready=0
for attempt in 1 2 3 4 5 6 7 8 9 10; do
    if curl -fsS -o /dev/null "http://127.0.0.1:$PORT/v1/version" 2>/dev/null; then
        ready=1
        break
    fi
    sleep 1
done
if [ "$ready" -ne 1 ]; then
    echo "adapter-coalesce gate: server did not start" >&2
    cat "$LOG" >&2
    exit 1
fi

curl -fsS -X POST "http://127.0.0.1:$PORT/v1/terminal-sessions" \
    -H 'Content-Type: application/json' \
    -H "Idempotency-Key: coalesce-$$" \
    -d '{"command":"/bin/cat","display_id":"display-1","surface":{"width":64,"height":24}}' \
    -o "$SESSION"
surface=$(sed -n 's/.*"surface_id":"\([^"]*\)".*/\1/p' "$SESSION")
session=$(sed -n 's/.*"id":"\([^"]*\)".*/\1/p' "$SESSION")
if [ -z "$surface" ]; then
    echo "adapter-coalesce gate: no surface in $(cat "$SESSION")" >&2
    exit 1
fi

# set -e would abort before the session is deleted, so take the status by hand.
if BLIT_SURFACE_ID="$surface" BLIT_SERVER_HOST=127.0.0.1 \
       BLIT_SERVER_PORT="$PORT" "$PROBE"; then
    status=0
else
    status=$?
fi

curl -fsS -X DELETE "http://127.0.0.1:$PORT/v1/terminal-sessions/$session" \
    -o /dev/null 2>/dev/null || true
exit "$status"
