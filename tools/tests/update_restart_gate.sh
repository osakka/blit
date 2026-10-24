#!/bin/sh
set -eu
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/gate_port.sh"
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/gate_display.sh"
gate_display_setup

# Issue #63. Two mechanisms used to restart the service after a package
# install and they raced: the privileged helper runs `systemctl restart`,
# which is the only path that can pick up a changed unit file, and the server
# also re-exec'd itself. systemd sends its one SIGTERM to the main pid, the
# process execs instead of exiting, and the unit waits out TimeoutStopSec and
# then SIGKILLs the whole cgroup -- taking an attached Hibr desktop with it,
# ninety seconds after the deployment reported success.
#
# So the two intents must stay distinguishable, and this gate is the only
# place that says which is which:
#
#   an update install  ->  exit, non-zero, no exec
#   POST /v1/control/restart  ->  exec, same pid, keeps serving
#
# The non-zero exit is deliberate: Restart=on-failure is the safety net if the
# helper dies before it reaches systemctl, because the package must never be
# installed with the service down.

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
binary=${BLIT_GATE_BINARY:-$root/build/blit/blit-server}
port=$(gate_port "${BLIT_UPDATE_RESTART_PORT:-19205}")
work=$(mktemp -d "${TMPDIR:-/tmp}/blit-update-restart.XXXXXX")
log=$work/server.log
helper_log=$work/helper.log
token_file=$work/token
socket=$work/update.sock
package=${BLIT_GATE_PACKAGE:-}
pid=
helper_pid=

cleanup()
{
    gate_display_cleanup
    [ -n "$helper_pid" ] && { kill -TERM "$helper_pid" 2>/dev/null || true; }
    [ -n "$pid" ] && { kill -TERM "$pid" 2>/dev/null || true; }
    # The stub is a python child of a subshell on some shells, so name it by
    # the socket it was given rather than trusting $! alone.
    pkill -f "$socket" 2>/dev/null || true
    rm -rf "$work"
}
trap cleanup EXIT INT TERM

# The install path needs a package the server will accept: a real .deb whose
# Package field is blit. The release artifact is the honest fixture.
if [ -z "$package" ]; then
    for candidate in "$root"/blit_*_amd64.deb; do
        [ -f "$candidate" ] && package=$candidate
    done
fi
if [ -z "$package" ] || [ ! -f "$package" ]; then
    echo "UPDATE_RESTART_SKIP no blit .deb to install; build one or set BLIT_GATE_PACKAGE"
    exit 77
fi

printf '%s\n' test-token >"$token_file"

# A stub for the privileged helper. The real one installs the package and
# runs `systemctl restart`; this one only answers OK, which is the part the
# server reacts to.
stub_helper()
{
    python3 - "$socket" "$helper_log" <<'PY'
import os, socket, sys
path, log = sys.argv[1], sys.argv[2]
try:
    os.unlink(path)
except FileNotFoundError:
    pass
server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
server.bind(path)
server.listen(4)
with open(log, 'w') as handle:
    handle.write('listening\n')
    handle.flush()
    while True:
        client, _ = server.accept()
        request = client.recv(4096).decode('utf-8', 'replace')
        handle.write('request %s' % request)
        handle.flush()
        client.sendall(b'OK\n')
        client.close()
PY
}

# Detached from this script's stdout: a backgrounded child that inherits it
# keeps a pipeline consumer (`| head`) waiting for EOF long after the gate has
# finished, which looks exactly like a hung gate.
stub_helper >>"$helper_log" 2>&1 </dev/null &
helper_pid=$!
i=0
while [ ! -S "$socket" ] && [ "$i" -lt 50 ]; do i=$((i + 1)); sleep 0.2; done
test -S "$socket"

start_server()
{
    BLIT_UPDATE_TOKEN_FILE="$token_file" BLIT_UPDATE_DIRECTORY="$work" \
        BLIT_UPDATE_SOCKET="$socket" env $GATE_DISPLAY_ENV \
        LD_LIBRARY_PATH="$root/build/sdl3-kms" \
        "$binary" "$port" >>"$log" 2>&1 &
    pid=$!
    i=0
    while [ "$i" -lt 60 ]; do
        if curl --connect-timeout 1 --max-time 2 --fail --silent \
            "http://127.0.0.1:$port/v1/version" >/dev/null 2>&1; then
            return 0
        fi
        i=$((i + 1))
        sleep 0.2
    done
    echo "UPDATE_RESTART_FAIL server did not become ready" >&2
    cat "$log" >&2
    exit 1
}

# --- POST /v1/control/restart execs: the pid survives and keeps serving ---
start_server
restart_pid=$pid
curl --fail --silent --show-error --request POST \
    "http://127.0.0.1:$port/v1/control/restart" >/dev/null
i=0
served=0
while [ "$i" -lt 60 ]; do
    if curl --connect-timeout 1 --max-time 2 --fail --silent \
        "http://127.0.0.1:$port/v1/version" >/dev/null 2>&1 &&
       kill -0 "$restart_pid" 2>/dev/null; then
        served=1
        break
    fi
    i=$((i + 1))
    sleep 0.2
done
if [ "$served" != 1 ]; then
    echo "UPDATE_RESTART_FAIL an API restart did not keep the pid serving" >&2
    cat "$log" >&2
    exit 1
fi
kill -TERM "$pid" 2>/dev/null || true
wait "$pid" 2>/dev/null || true
pid=

# --- an update install exits instead: the pid is gone, non-zero ---
start_server
install_pid=$pid
sha=$(sha256sum "$package" | awk '{print $1}')
status=$(curl --silent --show-error --request PUT \
    -H 'Content-Type: application/vnd.debian.binary-package' \
    -H 'X-Blit-Update-Token: test-token' -H "X-Blit-SHA256: $sha" \
    --data-binary "@$package" -w '\nHTTP=%{http_code}' \
    "http://127.0.0.1:$port/v1/control/update")
if ! printf '%s\n' "$status" | grep -q 'HTTP=202'; then
    echo "UPDATE_RESTART_FAIL the install was not accepted: $status" >&2
    cat "$log" "$helper_log" >&2
    exit 1
fi
if ! printf '%s\n' "$status" | grep -q '"state":"restarting"'; then
    echo "UPDATE_RESTART_FAIL unexpected install response: $status" >&2
    exit 1
fi
if ! grep -q 'request UPDATE ' "$helper_log"; then
    echo "UPDATE_RESTART_FAIL the helper was never asked to install" >&2
    cat "$helper_log" >&2
    exit 1
fi

# The process must leave. An exec keeps the pid alive and answering, which is
# exactly what raced systemd's stop.
exited=0
code=
i=0
while [ "$i" -lt 100 ]; do
    if ! kill -0 "$install_pid" 2>/dev/null; then
        exited=1
        break
    fi
    i=$((i + 1))
    sleep 0.2
done
if [ "$exited" != 1 ]; then
    if curl --connect-timeout 1 --max-time 2 --fail --silent \
        "http://127.0.0.1:$port/v1/version" >/dev/null 2>&1; then
        echo "UPDATE_RESTART_FAIL the server re-exec'd after an install and is" \
             "still serving; this is the race that SIGKILLs the unit" >&2
    else
        echo "UPDATE_RESTART_FAIL the server neither exited nor served" >&2
    fi
    cat "$log" >&2
    exit 1
fi
set +e
wait "$install_pid"
code=$?
set -e
pid=
if [ "$code" = 0 ]; then
    echo "UPDATE_RESTART_FAIL the install exit was 0, so Restart=on-failure" \
         "would not recover a helper that died before systemctl" >&2
    exit 1
fi
if curl --connect-timeout 1 --max-time 2 --fail --silent \
    "http://127.0.0.1:$port/v1/version" >/dev/null 2>&1; then
    echo "UPDATE_RESTART_FAIL something is still serving the port" >&2
    exit 1
fi

printf 'UPDATE_RESTART_PASS api_restart=exec_same_pid install=exit_code_%s\n' \
    "$code"
