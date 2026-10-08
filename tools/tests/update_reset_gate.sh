#!/bin/sh
set -eu

binary=${BLIT_GATE_BINARY:-build/blit/blit-server}
port=${BLIT_UPDATE_RESET_PORT:-19197}
log=${TMPDIR:-/tmp}/blit-update-reset-gate.$$.log
token_file=${TMPDIR:-/tmp}/blit-update-reset-gate.$$.token
bad_package=${TMPDIR:-/tmp}/blit-update-reset-gate.$$.deb
pid=

cleanup()
{
    if [ -n "${pid}" ]; then
        kill -TERM "${pid}" 2>/dev/null || true
        wait "${pid}" 2>/dev/null || true
    fi
    rm -f "$log" "$token_file" "$bad_package"
}
trap cleanup EXIT INT TERM

printf '%s\n' test-token >"$token_file"
printf '%s\n' not-a-debian-package >"$bad_package"
BLIT_UPDATE_TOKEN_FILE="$token_file" BLIT_UPDATE_DIRECTORY="${TMPDIR:-/tmp}" \
    SDL_VIDEODRIVER=dummy LD_LIBRARY_PATH=build/sdl3-kms \
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

sha=$(sha256sum "$bad_package" | awk '{print $1}')
status=$(curl --silent --show-error --request PUT \
    -H 'Content-Type: application/vnd.debian.binary-package' \
    -H 'X-Blit-Update-Token: test-token' -H "X-Blit-SHA256: $sha" \
    --data-binary "@$bad_package" \
    -w '\nHTTP=%{http_code}' \
    "http://127.0.0.1:$port/v1/control/update")
printf '%s\n' "$status" | grep -q 'HTTP=422'
printf '%s\n' "$status" | grep -q 'update_invalid'

reset=$(curl --fail --silent --show-error --request POST \
    -H 'X-Blit-Update-Token: test-token' \
    "http://127.0.0.1:$port/v1/control/update/reset")
printf '%s\n' "$reset" | grep -q '"state":"ready"'

status=$(curl --fail --silent --show-error \
    "http://127.0.0.1:$port/v1/control/update/status")
printf '%s\n' "$status" | grep -q '"state":"ready"'
printf 'UPDATE_RESET_PASS failed_state_recovered=api\n'
