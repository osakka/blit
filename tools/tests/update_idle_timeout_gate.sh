#!/bin/sh
set -eu

binary=${BLIT_GATE_BINARY:-build/blit/blit-server}
port=${BLIT_UPDATE_IDLE_PORT:-19197}
log=${TMPDIR:-/tmp}/blit-update-idle.$$.log
status=${TMPDIR:-/tmp}/blit-update-idle.$$.status
token_file=${TMPDIR:-/tmp}/blit-update-idle.$$.token
client_pid=
pid=

cleanup()
{
    if [ -n "${client_pid}" ]; then
        kill "${client_pid}" 2>/dev/null || true
        wait "${client_pid}" 2>/dev/null || true
    fi
    if [ -n "${pid}" ]; then
        kill "${pid}" 2>/dev/null || true
        wait "${pid}" 2>/dev/null || true
    fi
    rm -f "$log" "$status" "$token_file"
}
trap cleanup EXIT INT TERM

printf '%s\n' test-token >"$token_file"
BLIT_UPDATE_TOKEN_FILE="$token_file" BLIT_UPDATE_DIRECTORY="${TMPDIR:-/tmp}" SDL_VIDEODRIVER=dummy \
    LD_LIBRARY_PATH=build/sdl3-kms "$binary" "$port" >"$log" 2>&1 &
pid=$!
ready=0
for attempt in 1 2 3 4 5 6 7 8 9 10; do
    if curl --connect-timeout 1 --max-time 2 --fail --silent \
        "http://127.0.0.1:$port/v1/server" >/dev/null 2>&1; then
        ready=1
        break
    fi
    sleep 1
done
test "$ready" = 1

# Send valid headers and deliberately hold the body open. The server must
# release the staged upload after its bounded idle interval.
ruby -rsocket -e '
  socket = TCPSocket.new("127.0.0.1", ARGV.fetch(0).to_i)
  socket.write("PUT /v1/control/update HTTP/1.1\r\n" \
               "Host: 127.0.0.1\r\n" \
               "Content-Type: application/vnd.debian.binary-package\r\n" \
               "Content-Length: 1000000\r\n" \
               "X-Blit-Update-Token: test-token\r\n" \
               "X-Blit-SHA256: " + ("0" * 64) + "\r\n" \
               "Connection: keep-alive\r\n\r\n")
  sleep 40
' "$port" >/dev/null 2>&1 &
client_pid=$!

timed_out=0
for attempt in 1 2 3 4 5 6 7 8; do
    curl --connect-timeout 1 --max-time 2 --fail --silent \
        "http://127.0.0.1:$port/v1/control/update/status" >"$status"
    if grep -q '"detail":"update upload idle timeout"' "$status"; then
        timed_out=1
        break
    fi
    sleep 5
done
test "$timed_out" = 1
printf 'UPDATE_IDLE_TIMEOUT_GATE_PASS\n'
