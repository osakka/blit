#!/bin/sh
set -eu
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/gate_port.sh"

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
# The binary under review, which is the one packaging/build-deb.sh ships.
# This used to default to build/blit-desktop-current/bin/blit-server, a
# staging copy that build-deb.sh checks but never refreshes -- so the one gate
# covering authentication, the command allowlist and BLIT_API_TOKEN injection
# was passing against a binary that was eighteen hours old and not the one
# being released (issue #59).
BINARY=${BLIT_GATE_BINARY:-$ROOT/build/blit/blit-server}
PORT=${BLIT_GATE_PORT:-19205}
PORT=$(gate_port "$PORT")
TOKEN=${BLIT_GATE_TOKEN:-blit-network-gate}
LOG=$(mktemp /tmp/blit-network-gate.XXXXXX)
UNAUTH=$(mktemp /tmp/blit-network-unauth.XXXXXX)
AUTH=$(mktemp /tmp/blit-network-auth.XXXXXX)
DIAGNOSTICS=$(mktemp /tmp/blit-network-diagnostics.XXXXXX)
RUNTIME_DIAGNOSTICS=$(mktemp /tmp/blit-network-runtime-diagnostics.XXXXXX)
CONSOLE_DIAGNOSTICS=$(mktemp /tmp/blit-network-console-diagnostics.XXXXXX)
GRAPHICS=$(mktemp /tmp/blit-network-graphics.XXXXXX)
AUDIO=$(mktemp /tmp/blit-network-audio.XXXXXX)
DISPLAYS=$(mktemp /tmp/blit-network-displays.XXXXXX)
COMMAND_DENIED=$(mktemp /tmp/blit-network-command-denied.XXXXXX)
COMMAND_ALLOWED=$(mktemp /tmp/blit-network-command-allowed.XXXXXX)
server_pid=
cleanup() {
    if [ -n "$server_pid" ]; then kill "$server_pid" 2>/dev/null || true; fi
    rm -f "$LOG" "$UNAUTH" "$AUTH" "$DIAGNOSTICS" "$RUNTIME_DIAGNOSTICS" "$GRAPHICS" "$AUDIO" \
        "$DISPLAYS" "$COMMAND_DENIED" "$COMMAND_ALLOWED" "$CONSOLE_DIAGNOSTICS"
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
console_unauth=$(curl -sS -o /dev/null -w '%{http_code}' \
    "http://127.0.0.1:$PORT/v1/diagnostics/console")
console_diagnostics=$(curl -sS -o "$CONSOLE_DIAGNOSTICS" -w '%{http_code}' \
    -H "Authorization: Bearer $TOKEN" \
    "http://127.0.0.1:$PORT/v1/diagnostics/console")
test "$console_unauth" = 401
test "$console_diagnostics" = 200
ruby -rjson -e '
  j=JSON.parse(File.read(ARGV.fetch(0)))
  abort "console diagnostic ownership drift" unless j.fetch("owner")=="unmanaged" &&
    j.fetch("vt_number")==0 && j.fetch("active")==false &&
    j.fetch("keyboard_muted")==false && j.fetch("keyboard_mode")==-1 &&
    j.fetch("keyboard_read_errno")==0 && j.fetch("previous_vt_number")==0 &&
    j.fetch("recovery_process")==false && j.fetch("watchdog_timeout_ms")==10000 &&
    j.fetch("evdev_exclusive")==false
' "$CONSOLE_DIAGNOSTICS"
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
ruby -rjson -e '
  compatibility = JSON.parse(File.read(ARGV.fetch(0))).fetch("hibr_compatibility")
  %w[pinned_version runtime_version pinned_abi runtime_abi probe compatible].each do |key|
    abort "network gate: missing Hibr compatibility field #{key}" unless compatibility.key?(key)
  end
  abort "network gate: invalid Hibr compatibility probe" unless
    %w[available unavailable].include?(compatibility.fetch("probe"))
' "$RUNTIME_DIAGNOSTICS"
# The performance document and its schema must agree, in both directions.
# They had drifted twice: input_keys_dropped and input_reports_dropped were
# emitted and not described at all, and last_sdl_present_ms /
# max_sdl_present_ms were described but left out of `required`. A counter the
# schema does not mention is a counter a client cannot rely on, and this is
# the only gate with both a running server and the schema to hand.
ruby -ryaml -rjson -e '
  emitted = JSON.parse(File.read(ARGV.fetch(0))).fetch("performance").keys.sort
  schema = YAML.load_file(ARGV.fetch(1))
      .dig("components", "schemas", "RuntimeDiagnostics",
           "properties", "performance")
  required = schema.fetch("required").sort
  described = schema.fetch("properties").keys.sort
  problems = []
  problems << "emitted but not described: #{(emitted - described).inspect}" unless
    (emitted - described).empty?
  problems << "described but not emitted: #{(described - emitted).inspect}" unless
    (described - emitted).empty?
  problems << "emitted but not required: #{(emitted - required).inspect}" unless
    (emitted - required).empty?
  problems << "required but not emitted: #{(required - emitted).inspect}" unless
    (required - emitted).empty?
  unless problems.empty?
    problems.each { |p| warn "network gate: performance schema: #{p}" }
    exit 1
  end
' "$RUNTIME_DIAGNOSTICS" "$ROOT/openapi/blit-0.1.yaml"
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

# ADR 0065. POST /v1/terminal-sessions execve's a client-supplied command by
# design, and the deployed service may run privileged, so an unauthenticated
# network listener is remote code execution as whomever Blit runs as. It used
# to warn on stderr and carry on, which is how the project's own host spent
# time network-reachable with authentication off (issue #25).
#
# Three postures, three servers. The first two need no token, so they cannot
# reuse the one above.
POSTURE_PORT=$((PORT + 2))
POSTURE_LOG=$(mktemp /tmp/blit-network-posture.XXXXXX)
POSTURE_BODY=$(mktemp /tmp/blit-network-posture-body.XXXXXX)
posture_pid=
posture_cleanup() {
    if [ -n "$posture_pid" ]; then kill "$posture_pid" 2>/dev/null || true; fi
    rm -f "$POSTURE_LOG" "$POSTURE_BODY"
    token_cleanup
}
trap posture_cleanup EXIT HUP INT TERM

# 1. No token and no requested address: safe by default, and still a working
#    server for development.
SDL_VIDEODRIVER=dummy LD_LIBRARY_PATH="$ROOT/build/sdl3-kms" \
    "$BINARY" "$POSTURE_PORT" >"$POSTURE_LOG" 2>&1 &
posture_pid=$!
ready=0
for attempt in 1 2 3 4 5 6 7 8 9 10; do
    if curl -fsS -o "$POSTURE_BODY" \
       "http://127.0.0.1:$POSTURE_PORT/v1/server" 2>/dev/null; then
        ready=1
        break
    fi
    sleep 1
done
if [ "$ready" -ne 1 ]; then
    echo "network gate: the default-posture server did not become ready" >&2
    cat "$POSTURE_LOG" >&2
    exit 1
fi
default_bind=$(ruby -rjson -e '
  puts JSON.parse(File.read(ARGV.fetch(0))).fetch("listener").fetch("bind")
' "$POSTURE_BODY")
default_auth=$(ruby -rjson -e '
  puts JSON.parse(File.read(ARGV.fetch(0))).fetch("listener").fetch("authentication")
' "$POSTURE_BODY")
if [ "$default_bind" != 127.0.0.1 ] || [ "$default_auth" != none ]; then
    echo "network gate: with no token the listener must be loopback, got" \
         "bind=$default_bind authentication=$default_auth" >&2
    exit 1
fi
kill "$posture_pid" 2>/dev/null || true
wait "$posture_pid" 2>/dev/null || true
posture_pid=

# 2. No token and an explicitly requested network address: refused. An
#    operator who asked for that cannot have meant it, and the server says
#    which of the three ways out to take.
# `set -e` would abort on the refusal itself, which is the result under test.
if SDL_VIDEODRIVER=dummy LD_LIBRARY_PATH="$ROOT/build/sdl3-kms" \
       BLIT_BIND_ADDRESS=0.0.0.0 \
       "$BINARY" $((POSTURE_PORT + 1)) >"$POSTURE_LOG" 2>&1; then
    echo "network gate: a network bind with no token must be refused" >&2
    cat "$POSTURE_LOG" >&2
    exit 1
fi
if ! grep -q 'refusing to listen' "$POSTURE_LOG"; then
    echo "network gate: the refusal does not say why" >&2
    cat "$POSTURE_LOG" >&2
    exit 1
fi

# 3. The override exists for a fixture with no network, has to be asked for by
#    name, and is reported where health is read rather than only on stderr.
SDL_VIDEODRIVER=dummy LD_LIBRARY_PATH="$ROOT/build/sdl3-kms" \
    BLIT_BIND_ADDRESS=0.0.0.0 BLIT_ALLOW_UNAUTHENTICATED_NETWORK=1 \
    "$BINARY" $((POSTURE_PORT + 2)) >"$POSTURE_LOG" 2>&1 &
posture_pid=$!
ready=0
for attempt in 1 2 3 4 5 6 7 8 9 10; do
    if curl -fsS -o "$POSTURE_BODY" \
       "http://127.0.0.1:$((POSTURE_PORT + 2))/v1/diagnostics" 2>/dev/null; then
        ready=1
        break
    fi
    sleep 1
done
if [ "$ready" -ne 1 ]; then
    echo "network gate: the override-posture server did not become ready" >&2
    cat "$POSTURE_LOG" >&2
    exit 1
fi
if ! ruby -rjson -e '
  exit JSON.parse(File.read(ARGV.fetch(0))).fetch("warnings")
           .include?("unauthenticated_network_listener") ? 0 : 1
' "$POSTURE_BODY"; then
    echo "network gate: an unauthenticated network listener is not reported" \
         "in the health warnings" >&2
    exit 1
fi
kill "$posture_pid" 2>/dev/null || true
wait "$posture_pid" 2>/dev/null || true
posture_pid=

# The authenticated server started at the top of this gate is the fourth
# posture: a token, an explicit network bind, and 401 for an unauthenticated
# request -- asserted above.
echo "NETWORK_AUTH_PASS unauth=$unauth auth=$auth diagnostics=$diagnostics runtime_diagnostics=$runtime_diagnostics graphics=$graphics audio=$audio displays=$displays command_denied=$command_denied command_allowed=$command_allowed api_token_injected=$injected default_bind=$default_bind network_without_token=refused override_warned=yes"
