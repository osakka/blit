#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
BINARY=${BLIT_GATE_BINARY:-$ROOT/build/blit-desktop-current/bin/blit-server}
PORT=${BLIT_GATE_PORT:-19202}
TOKEN=${BLIT_GATE_TOKEN:-blit-network-gate}
LOG=$(mktemp /tmp/blit-network-gate.XXXXXX)
UNAUTH=$(mktemp /tmp/blit-network-unauth.XXXXXX)
AUTH=$(mktemp /tmp/blit-network-auth.XXXXXX)
DIAGNOSTICS=$(mktemp /tmp/blit-network-diagnostics.XXXXXX)
GRAPHICS=$(mktemp /tmp/blit-network-graphics.XXXXXX)
server_pid=
cleanup() {
    if [ -n "$server_pid" ]; then kill "$server_pid" 2>/dev/null || true; fi
    rm -f "$LOG" "$UNAUTH" "$AUTH" "$DIAGNOSTICS" "$GRAPHICS"
}
trap cleanup EXIT HUP INT TERM

if [ ! -x "$BINARY" ]; then
    echo "network gate: missing executable $BINARY" >&2
    exit 1
fi

SDL_VIDEODRIVER=dummy LD_LIBRARY_PATH="$ROOT/build/sdl3-kms" \
    BLIT_BIND_ADDRESS=0.0.0.0 BLIT_AUTH_TOKEN="$TOKEN" \
    "$BINARY" "$PORT" >"$LOG" 2>&1 &
server_pid=$!
ready=0
for attempt in 1 2 3 4 5 6 7 8 9 10; do
    if curl -fsS -o /dev/null "http://127.0.0.1:$PORT/v1/server" \
       -H "Authorization: Bearer $TOKEN" 2>/dev/null; then
        ready=1
        break
    fi
    sleep 1
done
if [ "$ready" -ne 1 ]; then
    echo "network gate: server did not become ready" >&2
    cat "$LOG" >&2
    exit 1
fi

unauth=$(curl -sS -o "$UNAUTH" -w '%{http_code}' \
    "http://127.0.0.1:$PORT/v1/server")
auth=$(curl -sS -o "$AUTH" -w '%{http_code}' \
    -H "Authorization: Bearer $TOKEN" \
    "http://127.0.0.1:$PORT/v1/server")
diagnostics=$(curl -sS -o "$DIAGNOSTICS" -w '%{http_code}' \
    -H "Authorization: Bearer $TOKEN" \
    "http://127.0.0.1:$PORT/v1/diagnostics")
graphics=$(curl -sS -o "$GRAPHICS" -w '%{http_code}' \
    -H "Authorization: Bearer $TOKEN" \
    "http://127.0.0.1:$PORT/v1/graphics")
if [ "$unauth" != 401 ] || [ "$auth" != 200 ] ||
   [ "$diagnostics" != 200 ] || [ "$graphics" != 200 ] ||
   ! grep -q '"health"' "$DIAGNOSTICS" ||
   ! grep -q '"graphics_cards"' "$GRAPHICS"; then
    echo "network gate: expected unauth=401 auth=200, got $unauth/$auth" >&2
    exit 1
fi
echo "NETWORK_AUTH_PASS unauth=$unauth auth=$auth diagnostics=$diagnostics graphics=$graphics"
