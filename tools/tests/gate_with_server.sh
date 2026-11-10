#!/bin/sh
# Run one command against a server this script starts and stops (ADR 0216).
#
#   gate_with_server.sh PORT COMMAND [ARGUMENT ...]
#
# terminal_conformance.rb and terminal_soak_gate.rb assert against a server
# they do not start. run-gates.sh used to own that server inline, in the middle
# of its serial loop. A gate that is dispatched to a worker, and bounded by a
# timeout, has to be one command, so the server's lifecycle lives here: PORT is
# a fixed gate port that BLIT_GATE_PORT_OFFSET shifts like every other, the
# command sees BLIT_TEST_SERVER and BLIT_SOAK_PID, and the server dies with this
# script whether the command passes, fails or is killed.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
BINARY=${BLIT_GATE_BINARY:-$ROOT/build/blit/blit-server}
FONT=${BLIT_GATE_FONT:-$ROOT/assets/fonts/Blit-Terminus16.psf}
GATE_DISPLAY_SOURCE=$ROOT/tools/tests/gate_display.sh
. "$ROOT/tools/tests/gate_display.sh"
. "$ROOT/tools/tests/gate_port.sh"
[ "$#" -ge 2 ] || { echo "usage: gate_with_server.sh PORT COMMAND [ARGUMENT ...]" >&2; exit 2; }
port=$(gate_port "$1")
shift

tmpdir=$(mktemp -d "${TMPDIR:-/tmp}/blit-with-server.XXXXXX")
server_pid=''
cleanup() {
    status=$?
    trap - EXIT
    if [ -n "$server_pid" ]; then
        kill "$server_pid" 2>/dev/null || true
        wait "$server_pid" 2>/dev/null || true
    fi
    gate_display_cleanup
    rm -rf "$tmpdir"
    exit "$status"
}
trap cleanup EXIT
trap 'exit 143' HUP INT TERM

gate_display_setup || { echo "gate_with_server: display setup failed" >&2; exit 1; }
env $GATE_SERVER_ENV BLIT_PSF_FONT="$FONT" $GATE_DISPLAY_ENV "$BINARY" "$port" >"$tmpdir/server.log" 2>&1 &
server_pid=$!
i=0
while [ "$i" -lt 50 ]; do
    if curl --fail --silent --max-time 1 "http://127.0.0.1:$port/v1/version" >/dev/null 2>&1; then
        break
    fi
    i=$((i + 1))
    sleep 0.2
done
if [ "$i" -ge 50 ]; then
    echo "gate_with_server: server did not become ready on port $port" >&2
    cat "$tmpdir/server.log" >&2
    exit 1
fi
BLIT_TEST_SERVER="http://127.0.0.1:$port" BLIT_SOAK_PID="$server_pid" "$@"
