#!/bin/sh
set -eu
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/gate_port.sh"
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/gate_display.sh"
gate_display_setup

binary=${BLIT_GATE_BINARY:-build/blit/blit-server}
port=${BLIT_UPDATE_CANCEL_PORT:-19195}
port=$(gate_port "$port")
log=${TMPDIR:-/tmp}/blit-update-cancel-gate.$$.log
status=${TMPDIR:-/tmp}/blit-update-cancel-gate.$$.status
token_file=${TMPDIR:-/tmp}/blit-update-cancel-gate.$$.token
client_pid=
pid=

cleanup()
{
    gate_display_cleanup
    if [ -n "${client_pid}" ]; then
        kill "${client_pid}" 2>/dev/null || true
        wait "${client_pid}" 2>/dev/null || true
    fi
    if [ -n "${pid}" ]; then
        kill -TERM "${pid}" 2>/dev/null || true
        wait "${pid}" 2>/dev/null || true
    fi
    rm -f "$log" "$status" "$token_file"
}
trap cleanup EXIT INT TERM

printf '%s\n' test-token >"$token_file"
BLIT_UPDATE_TOKEN_FILE="$token_file" BLIT_UPDATE_DIRECTORY="${TMPDIR:-/tmp}" env $GATE_DISPLAY_ENV \
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

# Hold a deliberately incomplete upload open. The server must remain
# responsive and expose bounded progress while the upload is stalled.
ruby -rsocket -e '
  s = TCPSocket.new("127.0.0.1", ARGV[0].to_i)
  s.write("PUT /v1/control/update HTTP/1.1\r\n" \
          "Host: 127.0.0.1\r\n" \
          "Content-Type: application/vnd.debian.binary-package\r\n" \
          "Content-Length: 1000000\r\n" \
          "X-Blit-Update-Token: test-token\r\n" \
          "X-Blit-SHA256: " + ("0" * 64) + "\r\n" \
          "Connection: close\r\n\r\n")
  sleep 30
' "$port" >/dev/null 2>&1 &
client_pid=$!

active=0
for attempt in 1 2 3 4 5; do
    curl --connect-timeout 1 --max-time 2 --fail --silent \
        "http://127.0.0.1:$port/v1/control/update/status" >"$status"
    if grep -q '"active":true' "$status"; then
        active=1
        break
    fi
    sleep 1
done
test "$active" = 1
grep -q '"expected_bytes":1000000' "$status"

response=$(curl --connect-timeout 2 --max-time 3 --fail --silent --show-error \
    --request POST --header 'X-Blit-Update-Token: test-token' \
    "http://127.0.0.1:$port/v1/control/update/cancel")
printf '%s\n' "$response" | grep -q '"state":"cancelled"'

curl --connect-timeout 2 --max-time 3 --fail --silent \
    "http://127.0.0.1:$port/v1/control/update/status" >"$status"
grep -q '"active":false' "$status"
printf 'UPDATE_CANCEL_GATE_PASS progress=exposed cancellation=202\n'
