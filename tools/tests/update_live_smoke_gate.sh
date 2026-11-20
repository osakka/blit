#!/bin/sh
# update-live.sh's smoke reading (Gitea #27) against a fake server: a box at
# its login screen waiting for a password reads login-screen-up 1; a signed-in
# desktop, a login screen with no surface, or a box without /v1/login read 0.
# The 0.3.127 deploy failed a healthy signed-out box under the old reading.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d)
trap 'kill $server 2>/dev/null || true; rm -rf "$work"' EXIT
cat > "$work/fake.py" <<'PY'
import http.server, json, sys
LOGIN = {
    'signed-out': {'state': 'running', 'session': {'surface_id': 'surface-1'},
                   'desktop': {'state': 'inactive', 'reason': 'authentication_required'}},
    'signed-in': {'state': 'running', 'session': {'surface_id': 'surface-1'},
                  'desktop': {'state': 'ready', 'reason': 'observed'}},
    'no-surface': {'state': 'running', 'session': {'surface_id': None},
                   'desktop': {'state': 'inactive', 'reason': 'authentication_required'}},
}
class H(http.server.BaseHTTPRequestHandler):
    def log_message(self, *a): pass
    def do_GET(self):
        body = {'/v1/server': {'display_backend': {'ready': 1}, 'display_count': 2},
                '/v1/topology': {'panes': [{'surface_id': 'surface-1'}, {'surface_id': None}]},
                '/v1/diagnostics/runtime': {'performance': {'frames': 42}},
                '/v1/login': LOGIN.get(open(sys.argv[2]).read().strip())}.get(self.path)
        if body is None:
            self.send_response(404); self.end_headers(); return
        data = json.dumps(body).encode()
        self.send_response(200); self.send_header('Content-Type', 'application/json')
        self.send_header('Content-Length', str(len(data))); self.end_headers(); self.wfile.write(data)
http.server.HTTPServer(('127.0.0.1', int(sys.argv[1])), H).serve_forever()
PY
port=$(python3 -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1",0)); print(s.getsockname()[1])')
python3 "$work/fake.py" "$port" "$work/case" & server=$!
base_url=http://127.0.0.1:$port api_token=
eval "$(sed -n '/^curl_auth()/,/^}/p' "$ROOT/tools/deploy/update-live.sh")"
eval "$(sed -n '/^smoke_state()/,/^}/p' "$ROOT/tools/deploy/update-live.sh")"
for case in signed-out signed-in no-surface missing; do
    echo "$case" > "$work/case"
    i=0; until curl -s -o /dev/null "$base_url/v1/server" || [ $i -ge 50 ]; do i=$((i+1)); sleep 0.1; done
    case $case in signed-out) want='1 2 1 42 1' ;; *) want='1 2 1 42 0' ;; esac
    got=$(smoke_state)
    [ "$got" = "$want" ] || { echo "UPDATE_LIVE_SMOKE_FAIL $case: got '$got', want '$want'"; exit 1; }
done
echo "UPDATE_LIVE_SMOKE_PASS cases=4 login_screen_up=signed-out"
