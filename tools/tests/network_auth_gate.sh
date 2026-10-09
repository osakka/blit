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
RUNTIME_DIAGNOSTICS=$(mktemp /tmp/blit-network-runtime-diagnostics.XXXXXX)
GRAPHICS=$(mktemp /tmp/blit-network-graphics.XXXXXX)
AUDIO=$(mktemp /tmp/blit-network-audio.XXXXXX)
DISPLAYS=$(mktemp /tmp/blit-network-displays.XXXXXX)
COMMAND_DENIED=$(mktemp /tmp/blit-network-command-denied.XXXXXX)
COMMAND_ALLOWED=$(mktemp /tmp/blit-network-command-allowed.XXXXXX)
server_pid=
cleanup() {
    if [ -n "$server_pid" ]; then kill "$server_pid" 2>/dev/null || true; fi
    rm -f "$LOG" "$UNAUTH" "$AUTH" "$DIAGNOSTICS" "$RUNTIME_DIAGNOSTICS" "$GRAPHICS" "$AUDIO" \
        "$DISPLAYS" "$COMMAND_DENIED" "$COMMAND_ALLOWED"
}
trap cleanup EXIT HUP INT TERM

if [ ! -x "$BINARY" ]; then
    echo "network gate: missing executable $BINARY" >&2
    exit 1
fi

SDL_VIDEODRIVER=dummy LD_LIBRARY_PATH="$ROOT/build/sdl3-kms" \
    BLIT_BIND_ADDRESS=0.0.0.0 BLIT_AUTH_TOKEN="$TOKEN" \
    BLIT_COMMAND_ALLOWLIST=/bin/true \
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

BLIT_GATE_PORT="$PORT" BLIT_GATE_TOKEN="$TOKEN" \
    ruby "$ROOT/tools/tests/http_fragmentation_gate.rb" "$PORT"
BLIT_GATE_PORT="$PORT" BLIT_GATE_TOKEN="$TOKEN" \
    ruby "$ROOT/tools/tests/http_backpressure_gate.rb" "$PORT"

unauth=$(curl -sS -o "$UNAUTH" -w '%{http_code}' \
    "http://127.0.0.1:$PORT/v1/server")
auth=$(curl -sS -o "$AUTH" -w '%{http_code}' \
    -H "Authorization: Bearer $TOKEN" \
    "http://127.0.0.1:$PORT/v1/server")
diagnostics=$(curl -sS -o "$DIAGNOSTICS" -w '%{http_code}' \
    -H "Authorization: Bearer $TOKEN" \
    "http://127.0.0.1:$PORT/v1/diagnostics")
runtime_diagnostics=$(curl -sS -o "$RUNTIME_DIAGNOSTICS" -w '%{http_code}' \
    -H "Authorization: Bearer $TOKEN" \
    "http://127.0.0.1:$PORT/v1/diagnostics/runtime")
graphics=$(curl -sS -o "$GRAPHICS" -w '%{http_code}' \
    -H "Authorization: Bearer $TOKEN" \
    "http://127.0.0.1:$PORT/v1/graphics")
audio=$(curl -sS -o "$AUDIO" -w '%{http_code}' \
    -H "Authorization: Bearer $TOKEN" \
    "http://127.0.0.1:$PORT/v1/audio")
displays=$(curl -sS -o "$DISPLAYS" -w '%{http_code}' \
    -H "Authorization: Bearer $TOKEN" \
    "http://127.0.0.1:$PORT/v1/displays")
if [ "$unauth" != 401 ] || [ "$auth" != 200 ] ||
   [ "$diagnostics" != 200 ] || [ "$runtime_diagnostics" != 200 ] || [ "$graphics" != 200 ] ||
   [ "$audio" != 200 ] || [ "$displays" != 200 ] ||
   ! grep -q '"health"' "$DIAGNOSTICS" ||
   ! grep -q '"presentation_sync":' "$DIAGNOSTICS" ||
   ! grep -q '"performance":' "$RUNTIME_DIAGNOSTICS" ||
   ! grep -q '"stream_queue_bytes":' "$RUNTIME_DIAGNOSTICS" ||
   ! grep -q '"logs":' "$RUNTIME_DIAGNOSTICS" ||
   ! grep -q '"graphics_cards"' "$GRAPHICS" ||
   ! grep -q '"devices"' "$AUDIO" ||
   ! grep -q '"modes":\[' "$DISPLAYS" ||
   ! grep -q '"active_mode":' "$DISPLAYS"; then
    echo "network gate: expected unauth=401 auth=200 and diagnostics routes=200, got $unauth/$auth/$diagnostics/$runtime_diagnostics" >&2
    exit 1
fi
ruby -rjson -e 'JSON.parse(File.read(ARGV.fetch(0)))' "$RUNTIME_DIAGNOSTICS"
command_denied=$(curl -sS -o "$COMMAND_DENIED" -w '%{http_code}' \
    -H "Authorization: Bearer $TOKEN" \
    -H 'Content-Type: application/json' \
    --data '{"command":"/bin/sh","surface":{"width":80,"height":24}}' \
    "http://127.0.0.1:$PORT/v1/terminal-sessions")
command_allowed=$(curl -sS -o "$COMMAND_ALLOWED" -w '%{http_code}' \
    -H "Authorization: Bearer $TOKEN" \
    -H 'Content-Type: application/json' \
    --data '{"command":"/bin/true","surface":{"width":80,"height":24}}' \
    "http://127.0.0.1:$PORT/v1/terminal-sessions")
if [ "$command_denied" != 403 ] || [ "$command_allowed" != 201 ] ||
   ! grep -q '"code":"command_not_allowed"' "$COMMAND_DENIED"; then
    echo "network gate: command policy failed denied=$command_denied allowed=$command_allowed" >&2
    exit 1
fi
# ADR 0055: a configured token also gates the owner handshake and the stream
# upgrade, so Blit injects BLIT_API_TOKEN into the launched child and the Hibr
# adapter presents it. Assert the injection here, where a token is already
# configured. This needs a child that can report its own environment, so it
# runs against a second server whose allowlist permits a shell; the allowlist
# assertions above deliberately keep /bin/sh denied.
#
# The check reports presence and length only. It must never print the value:
# the surface it reads is returned by the API in full.
TOKEN_PORT=$((PORT + 1))
TOKEN_SURFACE=$(mktemp /tmp/blit-network-token-surface.XXXXXX)
token_server_pid=
token_cleanup() {
    if [ -n "$token_server_pid" ]; then kill "$token_server_pid" 2>/dev/null || true; fi
    rm -f "$TOKEN_SURFACE"
    cleanup
}
trap token_cleanup EXIT HUP INT TERM

SDL_VIDEODRIVER=dummy LD_LIBRARY_PATH="$ROOT/build/sdl3-kms" \
    BLIT_AUTH_TOKEN="$TOKEN" BLIT_COMMAND_ALLOWLIST=/bin/sh \
    "$BINARY" "$TOKEN_PORT" >>"$LOG" 2>&1 &
token_server_pid=$!
ready=0
for attempt in 1 2 3 4 5 6 7 8 9 10; do
    if curl -fsS -o /dev/null "http://127.0.0.1:$TOKEN_PORT/v1/version" \
       -H "Authorization: Bearer $TOKEN" 2>/dev/null; then
        ready=1
        break
    fi
    sleep 1
done
if [ "$ready" -ne 1 ]; then
    echo "network gate: token-injection server did not become ready" >&2
    exit 1
fi

token_session=$(curl -fsS -H "Authorization: Bearer $TOKEN" \
    -H 'Content-Type: application/json' \
    --data '{"command":"/bin/sh","surface":{"width":80,"height":24}}' \
    "http://127.0.0.1:$TOKEN_PORT/v1/terminal-sessions")
token_session_id=$(printf '%s' "$token_session" |
    sed -n 's/.*"id":"\([^"]*\)".*/\1/p')
token_surface_id=$(printf '%s' "$token_session" |
    sed -n 's/.*"surface_id":"\([^"]*\)".*/\1/p')
if [ -z "$token_session_id" ] || [ -z "$token_surface_id" ]; then
    echo "network gate: could not create the token-injection session" >&2
    exit 1
fi
if printf '%s' "$token_session" | grep -qF "$TOKEN"; then
    echo "network gate: the session response leaked the bearer token" >&2
    exit 1
fi

curl -fsS -o /dev/null -H "Authorization: Bearer $TOKEN" \
    -H 'Content-Type: application/json' \
    --data '{"data":"stty -echo\n","mode":"raw"}' \
    "http://127.0.0.1:$TOKEN_PORT/v1/terminal-sessions/$token_session_id/input"
sleep 1
curl -fsS -o /dev/null -H "Authorization: Bearer $TOKEN" \
    -H 'Content-Type: application/json' \
    --data '{"data":"if [ -n \"$BLIT_API_TOKEN\" ]; then echo RESULT=PRESENT; else echo RESULT=ABSENT; fi\n","mode":"raw"}' \
    "http://127.0.0.1:$TOKEN_PORT/v1/terminal-sessions/$token_session_id/input"
sleep 2
curl -fsS -o "$TOKEN_SURFACE" -H "Authorization: Bearer $TOKEN" \
    "http://127.0.0.1:$TOKEN_PORT/v1/surfaces/$token_surface_id/cells"
injected=$(ruby -rjson -e '
  d = JSON.parse(File.read(ARGV.fetch(0)))
  g = Hash.new(" ")
  d.fetch("cells").each { |c| g[[c.fetch("x", 0), c.fetch("y", 0)]] = c["text"] || " " }
  rows = (0...d.fetch("height")).map { |y| (0...d.fetch("width")).map { |x| g[[x, y]] }.join.rstrip }
  puts(rows.any? { |r| r.include?("RESULT=PRESENT") } ? "PRESENT" :
       rows.any? { |r| r.include?("RESULT=ABSENT") } ? "ABSENT" : "UNKNOWN")
' "$TOKEN_SURFACE")
if [ "$injected" != PRESENT ]; then
    echo "network gate: BLIT_API_TOKEN was not injected into the child (got $injected)" >&2
    exit 1
fi
if grep -qF "$TOKEN" "$TOKEN_SURFACE"; then
    echo "network gate: the surface snapshot contains the bearer token" >&2
    exit 1
fi

echo "NETWORK_AUTH_PASS unauth=$unauth auth=$auth diagnostics=$diagnostics runtime_diagnostics=$runtime_diagnostics graphics=$graphics audio=$audio displays=$displays command_denied=$command_denied command_allowed=$command_allowed api_token_injected=$injected"
