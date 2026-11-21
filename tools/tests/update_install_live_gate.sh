#!/bin/sh
# tools/tests/update_install_live_gate.sh
#
# Security review SR-12 (Gitea #194), and the server's half of SR-21 (#203).
# The install used to run on the server's main loop: it waited for the root
# helper's answer with a blocking read and no timeout, so a slow dpkg froze
# the display and the API, and the console watchdog killed the server at
# 120 s, mid-install. Now the server asks the helper and goes on; the answer
# comes back through the main loop.
#
# A stub helper answers after BLIT_UPDATE_LIVE_SECONDS (default 150, past
# the old 120 s install lease). Throughout:
#   - GET /v1/version answers every second, within 2 s;
#   - a session printing ten lines a second keeps the display presenting
#     (performance.frames in /v1/diagnostics/runtime advances in every
#     10 s window);
#   - /v1/control/update/status says "installing";
#   - a second PUT, reset and cancel are refused 409 update_installing.
# Then the stub answers ERROR with 417 bytes of dpkg's output, and both the
# PUT's 500 and the status keep its end, the line naming the failing
# postinst (the 256-byte detail used to keep its start).
#
# The gate's server owns no VT, so there is no console watchdog here to
# outlast; the gate shows the loop never stops, which is what the watchdog
# measures.
set -eu
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/gate_port.sh"
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/gate_display.sh"
gate_display_setup

fail() { echo "UPDATE_INSTALL_LIVE_FAIL $*" >&2; exit 1; }

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
binary=${BLIT_GATE_BINARY:-$root/build/blit/blit-server}
port=$(gate_port "${BLIT_UPDATE_LIVE_PORT:-19273}")
seconds=${BLIT_UPDATE_LIVE_SECONDS:-150}
work=$(mktemp -d "${TMPDIR:-/tmp}/blit-update-live.XXXXXX")
socket=$work/update.sock
base=http://127.0.0.1:$port
pid=
helper_pid=
put_pid=

cleanup()
{
    gate_display_cleanup
    for p in "$put_pid" "$helper_pid" "$pid"; do
        [ -n "$p" ] && { kill -TERM "$p" 2>/dev/null || true; }
    done
    [ -n "$pid" ] && { wait "$pid" 2>/dev/null || true; }
    rm -rf "$work"
}
trap cleanup EXIT INT TERM

# The server checks each part is a readable Debian package named blit or
# blit-compositor before it asks the helper; a minimal pair is enough.
for name in blit blit-compositor; do
    mkdir -p "$work/$name/DEBIAN"
    printf 'Package: %s\nVersion: 9.9.9-1\nArchitecture: amd64\nMaintainer: gate <gate@localhost>\nDescription: update_install_live_gate fixture\n' \
        "$name" >"$work/$name/DEBIAN/control"
    dpkg-deb --build --root-owner-group "$work/$name" "$work/$name.deb" >/dev/null
done
printf '%s\n' test-token >"$work/token"

# The stub: takes the request, waits, answers the way the real helper does
# when dpkg fails a postinst -- the end of dpkg's output, flattened.
python3 - "$socket" "$seconds" >"$work/helper.log" 2>&1 </dev/null <<'PY' &
import os, socket, sys, time
path, seconds = sys.argv[1], float(sys.argv[2])
server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
server.bind(path)
server.listen(4)
print('listening', flush=True)
client, _ = server.accept()
print('request', client.recv(4096).decode('utf-8', 'replace').strip(), flush=True)
time.sleep(seconds)
head = 'Reading database ... 1234 files and directories currently installed. ' + \
    'Preparing to unpack .../update-AAAAAA ... Unpacking blit (9.9.9-1) over (9.9.8-1) ... ' * 2
tail = 'Setting up blit (9.9.9-1) ... dpkg: error processing package blit (--install): ' + \
    ' installed blit package post-installation script subprocess returned error exit status 1'
client.sendall(('ERROR ...' + head + tail + '\n').encode())
client.close()
print('answered', flush=True)
PY
helper_pid=$!
i=0
while [ ! -S "$socket" ] && [ "$i" -lt 100 ]; do i=$((i + 1)); sleep 0.1; done
[ -S "$socket" ] || fail "the stub helper did not listen"

env $GATE_SERVER_ENV $GATE_DISPLAY_ENV \
    BLIT_UPDATE_TOKEN_FILE="$work/token" BLIT_UPDATE_DIRECTORY="$work" \
    BLIT_UPDATE_SOCKET="$socket" \
    "$binary" "$port" >"$work/server.log" 2>&1 &
pid=$!
i=0
until curl --connect-timeout 1 --max-time 2 --fail --silent "$base/v1/version" >/dev/null 2>&1; do
    i=$((i + 1))
    [ "$i" -lt 60 ] || { cat "$work/server.log" >&2; fail "the server did not become ready"; }
    sleep 0.2
done

curl --fail --silent --show-error -X POST "$base/v1/terminal-sessions" \
    -H 'Content-Type: application/json' \
    -d '{"command":"/bin/sh","arguments":["-c","while :; do date +%s.%N; sleep 0.1; done"],"display_id":"display-1","surface":{"width":64,"height":24}}' \
    -o /dev/null || fail "could not start the printing session"

frames() {
    curl --connect-timeout 1 --max-time 2 --fail --silent "$base/v1/diagnostics/runtime" |
        python3 -c 'import json, sys; print(json.load(sys.stdin)["performance"]["frames"])'
}

part() { echo "$(wc -c <"$1" | tr -d ' '):$(sha256sum "$1" | cut -c1-64)"; }
packages="$(part "$work/blit.deb") $(part "$work/blit-compositor.deb")"
cat "$work/blit.deb" "$work/blit-compositor.deb" >"$work/body"
put() {   # OUTPUT: the PUT's body, then HTTP=<code>
    curl --silent --show-error --max-time "$((seconds + 60))" --request PUT \
        -H 'Content-Type: application/vnd.debian.binary-package' \
        -H 'X-Blit-Update-Token: test-token' \
        -H "X-Blit-Packages: $packages" \
        --data-binary "@$work/body" -w '\nHTTP=%{http_code}' \
        "$base/v1/control/update" >"$1" 2>&1
}
put "$work/put.out" &
put_pid=$!

i=0
until grep -q '^request UPDATE ' "$work/helper.log"; do
    i=$((i + 1))
    [ "$i" -lt 100 ] || { cat "$work/helper.log" "$work/server.log" >&2; fail "the helper was never asked"; }
    sleep 0.1
done
started=$(date +%s)

status=$(curl --max-time 2 --fail --silent "$base/v1/control/update/status") ||
    fail "status did not answer while installing"
case "$status" in *'"state":"installing"'*) ;; *) fail "status while installing: $status" ;; esac

second=$(curl --silent --max-time 5 --request PUT \
    -H 'Content-Type: application/vnd.debian.binary-package' \
    -H 'X-Blit-Update-Token: test-token' -H "X-Blit-Packages: $packages" \
    --data-binary "@$work/body" -w ' HTTP=%{http_code}' "$base/v1/control/update")
case "$second" in *update_installing*HTTP=409) ;; *) fail "a second PUT while installing: $second" ;; esac
for action in reset cancel; do
    answer=$(curl --silent --max-time 5 --request POST -H 'X-Blit-Update-Token: test-token' \
        -w ' HTTP=%{http_code}' "$base/v1/control/update/$action")
    case "$answer" in *update_installing*HTTP=409) ;; *) fail "$action while installing: $answer" ;; esac
done

last_frames=$(frames) || fail "diagnostics did not answer while installing"
window=$(date +%s)
polls=0
while kill -0 "$put_pid" 2>/dev/null; do
    now=$(date +%s)
    [ "$((now - started))" -lt "$((seconds + 30))" ] || fail "the PUT did not finish after the helper answered"
    if ! grep -q '^answered' "$work/helper.log"; then
        curl --connect-timeout 1 --max-time 2 --fail --silent "$base/v1/version" >/dev/null ||
            fail "GET /v1/version did not answer within 2 s, $((now - started)) s into the install"
        polls=$((polls + 1))
        if [ "$((now - window))" -ge 10 ]; then
            current=$(frames) || fail "diagnostics did not answer, $((now - started)) s into the install"
            [ "$current" -gt "$last_frames" ] ||
                fail "no frame presented from $((window - started)) s to $((now - started)) s into the install ($last_frames, $current)"
            last_frames=$current
            window=$now
        fi
    fi
    sleep 1
done
wait "$put_pid" || true
put_pid=
[ "$polls" -ge "$((seconds * 3 / 4))" ] || fail "only $polls polls in a $seconds s install"
kill -0 "$pid" 2>/dev/null || fail "the server is gone"

grep -q 'HTTP=500' "$work/put.out" || fail "the PUT's answer: $(cat "$work/put.out")"
grep -q 'update_install_failed' "$work/put.out" || fail "the PUT's problem: $(cat "$work/put.out")"
grep -q 'post-installation script subprocess returned error exit status 1' "$work/put.out" ||
    fail "the PUT's detail lost the end of dpkg's output: $(cat "$work/put.out")"
status=$(curl --max-time 2 --fail --silent "$base/v1/control/update/status")
case "$status" in
    *'"state":"failed"'*'post-installation script subprocess returned error exit status 1'*) ;;
    *) fail "status after the failed install: $status" ;;
esac
curl --max-time 2 --fail --silent "$base/v1/version" >/dev/null || fail "the API is gone after the install"

echo "UPDATE_INSTALL_LIVE_PASS install_seconds=$seconds version_polls=$polls frames=$last_frames second_put=409 reset=409 cancel=409 detail_tail=kept"
