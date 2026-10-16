#!/usr/bin/env bash
set -euo pipefail

root=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
source "$root/tools/diagnostics/http.sh"

port=$((20000 + ($$ % 1000)))
ready=$(mktemp /tmp/blit-diagnostic-auth-ready.XXXXXX)
out=$(mktemp /tmp/blit-diagnostic-auth-output.XXXXXX)
token_file=$(mktemp /tmp/blit-diagnostic-auth-token.XXXXXX)
unlink "$ready"
printf '%s\n' diagnostic-test-token >"$token_file"
server_pid=''
cleanup() {
    [ -z "$server_pid" ] || kill "$server_pid" 2>/dev/null || true
    [ -z "$server_pid" ] || wait "$server_pid" 2>/dev/null || true
    unlink "$ready" 2>/dev/null || true
    unlink "$out" 2>/dev/null || true
    unlink "$token_file" 2>/dev/null || true
}
trap cleanup EXIT HUP INT TERM

python3 - "$port" "$ready" <<'PY' &
import http.server
import pathlib
import sys

port = int(sys.argv[1])
ready = pathlib.Path(sys.argv[2])

class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        if self.headers.get('Authorization') != 'Bearer diagnostic-test-token':
            self.send_response(401)
            self.end_headers()
            return
        self.send_response(200)
        self.end_headers()
        self.wfile.write(b'authenticated')

    def log_message(self, *_):
        pass

server = http.server.HTTPServer(('127.0.0.1', port), Handler)
ready.touch()
server.serve_forever()
PY
server_pid=$!
for _ in 1 2 3 4 5 6 7 8 9 10; do
    [ -f "$ready" ] && break
    sleep 0.1
done
[ -f "$ready" ]

BLIT_AUTH_TOKEN_FILE=$token_file
diag_init_auth
diag_curl --fail --silent --show-error \
    "http://127.0.0.1:$port/health" >"$out"
grep -Fx 'authenticated' "$out"

echo 'DIAGNOSTIC_AUTH_PASS bearer_file=propagated'
