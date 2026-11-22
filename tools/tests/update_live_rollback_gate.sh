#!/bin/sh
# tools/deploy/update-live.sh's rollback (Gitea #206) and its judgement of a
# repaired box (Gitea #207), run whole against a fake Blit on 127.0.0.1:
# its update API installs a "release" by the SHA-256 of the blit package in
# X-Blit-Packages, and a release named in the gate's broken list comes up
# without its display (no display ready, no pane, frames standing still).
# Scratch packages (dpkg-deb --build, no root), throwaway 128-hex .sig
# files (the fake checks only that the right one is sent: verifying is the
# helper's, release_signature_gate and update_pair_gate) and manifests in a
# scratch copy of the tree. Nothing leaves this machine.
#
#   rollback       0.3.200 up; --rollback-to 0.3.200, install broken 0.3.201
#                  -> smoke failed; 0.3.200 again, sent with its own parts,
#                  .sig, bearer and update token; smoke passed against the
#                  box as it was before; exit 3
#   recovery       broken 0.3.201 up; update-live.sh 0.3.200, no option
#                  -> verified, smoke passed (the display back, none
#                  before), exit 0. Before #207: "smoke failed", exit 1
#   api-only       broken 0.3.201 up; 0.3.200 also comes up without a display
#                  -> smoke passed, "API only, as before", after the grace
#   baseline       broken 0.3.201 up; BLIT_SMOKE_BASELINE says two displays
#                  before the failed update; 0.3.200 brings back one
#                  -> smoke failed, exit 1: the recorded state is the judge
#   rollback-fails 0.3.200 up; 0.3.201 broken and 0.3.200 broken when
#                  installed again -> exit 4, "rollback failed"
#   update         0.3.200 up; --rollback-to 0.3.200, install 0.3.202
#                  -> smoke passed, exit 0, one upload
#   no-sig, floor, not-live
#                  --rollback-to a pair without its .sig, one below the
#                  helper's oldest release (0.3.132), or one that is not
#                  the version the box runs -> refused, exit 2, nothing sent
#
# BLIT_GATE_UPDATE_LIVE runs another update-live.sh (the one before the fix,
# to see the gate fail); BLIT_GATE_CASES runs only the cases it names.
set -eu
unset BLIT_SMOKE_BASELINE BLIT_SMOKE_SECONDS BLIT_UPDATE_TOKEN_FILE BLIT_AUTH_TOKEN_FILE
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
script=${BLIT_GATE_UPDATE_LIVE:-$ROOT/tools/deploy/update-live.sh}
work=$(mktemp -d "${TMPDIR:-/tmp}/blit-update-live-rollback.XXXXXX")
server=
trap 'kill $server 2>/dev/null || true; rm -rf "$work"' EXIT HUP INT TERM
fail() { echo "UPDATE_LIVE_ROLLBACK_FAIL $*"; sed -n '1,40p' "$work/live.log" 2>/dev/null; exit 1; }
want() {
    [ -z "${BLIT_GATE_CASES:-}" ] && return 0
    case " $BLIT_GATE_CASES " in *" $1 "*) return 0 ;; esac
    return 1
}

token=gate-api-token
update_token=gate-update-token
tree=$work/tree
mkdir -p "$tree/tools/deploy" "$tree/releases" "$tree/src"
cp "$script" "$tree/tools/deploy/update-live.sh"
cp "$ROOT/src/blit_update_helper.c" "$tree/src/"

# release VERSION DIR: the pair at VERSION-1 in DIR, its .sig and manifest;
# prints the blit package's SHA-256.
release() {
    for p in blit blit-compositor; do
        mkdir -p "$work/pkg/$p/DEBIAN"
        printf 'Package: %s\nVersion: %s-1\nArchitecture: amd64\nMaintainer: gate <gate@invalid>\nDescription: update_live_rollback_gate %s\n' \
            "$p" "$1" "$1" >"$work/pkg/$p/DEBIAN/control"
        dpkg-deb --build --root-owner-group "$work/pkg/$p" "$2/${p}_$1-1_amd64.deb" >/dev/null
        rm -rf "$work/pkg/$p"
    done
    printf '%s' "$1" | sha512sum | cut -c1-128 >"$2/blit-release_$1-1_amd64.sig"
    b=$(sha256sum "$2/blit_$1-1_amd64.deb" | cut -c1-64)
    c=$(sha256sum "$2/blit-compositor_$1-1_amd64.deb" | cut -c1-64)
    printf 'package: blit_%s-1_amd64.deb\npackage_sha256: %s\ncompositor_package: blit-compositor_%s-1_amd64.deb\ncompositor_package_sha256: %s\n' \
        "$1" "$b" "$1" "$c" >"$tree/releases/$1.yaml"
    printf '%s\n' "$b"
}
: >"$work/versions"
for v in 0.3.132 0.3.200 0.3.201 0.3.202 0.3.203; do
    mkdir -p "$work/r$v"
    printf '%s %s\n' "$(release "$v" "$work/r$v")" "$v" >>"$work/versions"
done
rm "$work/r0.3.203/blit-release_0.3.203-1_amd64.sig"   # no-sig

cat >"$work/fake.py" <<'PY'
import http.server, json, sys
port, work, token, update_token = int(sys.argv[1]), sys.argv[2], sys.argv[3], sys.argv[4]
versions = dict(line.split() for line in open(work + "/versions"))
box = {"version": "0.3.200", "broken": False, "frames": 0}

class H(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    def log_message(self, *a): pass
    def answer(self, code, body):
        data = json.dumps(body, separators=(",", ":")).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(data)
        self.close_connection = True
    def authorized(self):
        if self.headers.get("Authorization") == "Bearer " + token:
            return True
        self.answer(401, {"code": "unauthorized"})
        return False
    def do_GET(self):
        if self.path.startswith("/gate/set/"):   # the gate's own: which release is up
            box.update(version=self.path.rsplit("/", 1)[1], broken=self.path.startswith("/gate/set/broken/"))
            return self.answer(200, box)
        if not self.authorized():
            return
        up = not box["broken"]
        if up:
            box["frames"] += 1
        body = {
            "/v1/version": {"server_version": box["version"]},
            "/v1/server": {"display_backend": {"ready": int(up)}, "display_count": int(up)},
            "/v1/topology": {"panes": [{"surface_id": "surface-1" if up else None}]},
            "/v1/diagnostics/runtime": {"performance": {"frames": box["frames"]}},
            "/v1/login": {"state": "running", "session": {"surface_id": "surface-1"},
                          "desktop": {"reason": "authentication_required"}} if up else {"state": "stopped"},
            "/v1/control/update/status": {"state": "ready", "active": False},
        }.get(self.path)
        if body is None:
            return self.answer(404, {"code": "not_found"})
        self.answer(200, body)
    def do_PUT(self):
        length = int(self.headers.get("Content-Length", "0"))
        self.rfile.read(length)
        if self.path != "/v1/control/update" or not self.authorized():
            return
        if self.headers.get("X-Blit-Update-Token") != update_token:
            return self.answer(403, {"code": "update_forbidden"})
        parts = self.headers.get("X-Blit-Packages", "")
        version = versions.get(parts.split()[0].split(":")[1] if parts else "", "")
        with open(work + "/puts", "a") as log:
            log.write("%s %s %s\n" % (version, self.headers.get("X-Blit-Signature", ""), parts))
        if not version:
            return self.answer(400, {"code": "update_invalid"})
        broken = version in open(work + "/broken").read().split()
        box.update(version=version, broken=broken)
        self.answer(202, {"state": "installing"})

http.server.HTTPServer(("127.0.0.1", port), H).serve_forever()
PY
port=$(python3 -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1",0)); print(s.getsockname()[1])')
: >"$work/broken"
python3 "$work/fake.py" "$port" "$work" "$token" "$update_token" & server=$!
base=http://127.0.0.1:$port
i=0
until curl -s -o /dev/null "$base/gate/set/0.3.200"; do
    i=$((i + 1)); [ "$i" -lt 50 ] || fail "the fake server did not start"; sleep 0.1
done

# up STATE VERSION: the box as a case starts (STATE good or broken).
up() {
    if [ "$1" = broken ]; then curl -fs -o /dev/null "$base/gate/set/broken/$2"
    else curl -fs -o /dev/null "$base/gate/set/$2"; fi
    : >"$work/puts"
}
# live [ARGS...] -> update-live.sh's exit status in $status, output in live.log
live() {
    if env BLIT_BASE_URL="$base" BLIT_AUTH_TOKEN="$token" BLIT_UPDATE_TOKEN="$update_token" \
            BLIT_SMOKE_SECONDS=3 sh "$tree/tools/deploy/update-live.sh" "$@" >"$work/live.log" 2>&1; then
        status=0
    else
        status=$?
    fi
}
expect() {   # CASE STATUS PATTERN...
    c=$1 s=$2
    shift 2
    [ "$status" = "$s" ] || fail "$c: update-live.sh ended $status, expected $s"
    for p in "$@"; do grep -q -- "$p" "$work/live.log" || fail "$c: no '$p' in its output"; done
}
now_version() { curl -fs -H "Authorization: Bearer $token" "$base/v1/version" | sed -n 's/.*"server_version":"\([^"]*\)".*/\1/p'; }
sig() { cat "$work/r$1/blit-release_$1-1_amd64.sig"; }
deb() { printf '%s\n' "$work/r$1/blit_$1-1_amd64.deb"; }
cases=0

if want rollback; then
    up good 0.3.200
    printf '0.3.201\n' >"$work/broken"
    live --rollback-to "$(deb 0.3.200)" 127.0.0.1 "$(deb 0.3.201)"
    expect rollback 3 'update verified: Blit 0.3.201' 'smoke failed: installed Blit 0.3.201' \
        'rolling back to Blit 0.3.200' 'update verified: Blit 0.3.200' 'smoke passed: 1 displays up, 1 panes' \
        'rolled back: Blit 0.3.200'
    [ "$(now_version)" = 0.3.200 ] || fail "rollback: the box runs $(now_version)"
    [ "$(wc -l <"$work/puts")" = 2 ] || fail "rollback: $(wc -l <"$work/puts") uploads, expected 2"
    set -- $(sed -n 2p "$work/puts")
    [ "$1" = 0.3.200 ] && [ "$2" = "$(sig 0.3.200)" ] ||
        fail "rollback: the second upload was $1 with another signature than 0.3.200's own"
    cases=$((cases + 1))
    echo "case rollback: $(grep -m1 -o 'smoke failed: [^,]*' "$work/live.log"); $(grep 'rolled back' "$work/live.log" | cut -c1-60)...; exit 3"
fi

if want recovery; then
    up broken 0.3.201
    : >"$work/broken"
    live 127.0.0.1 "$(deb 0.3.200)"
    expect recovery 0 'update verified: Blit 0.3.200' 'smoke passed: 1 displays up (none before)'
    cases=$((cases + 1))
    echo "case recovery: $(grep 'smoke passed' "$work/live.log")"
fi

if want api-only; then
    # A box with no display, and a release that brings none: "API only, as
    # before" -- but only after the grace a restarting display may need.
    up broken 0.3.201
    printf '0.3.200\n' >"$work/broken"
    live 127.0.0.1 "$(deb 0.3.200)"
    expect api-only 0 'smoke passed: API only, as before'
    cases=$((cases + 1))
    echo "case api-only: $(grep 'smoke passed' "$work/live.log")"
fi

if want baseline; then
    up broken 0.3.201
    : >"$work/broken"
    export BLIT_SMOKE_BASELINE='1 2 1 0 1'
    live 127.0.0.1 "$(deb 0.3.200)"
    expect baseline 1 'update verified: Blit 0.3.200' 'smoke failed: installed Blit 0.3.200' "before (ready displays panes-with-surface frames login-screen-up) 1 2 1 0 1"
    BLIT_SMOKE_BASELINE='1 2 x 0 1'
    live 127.0.0.1 "$(deb 0.3.200)"
    expect baseline-invalid 2 'BLIT_SMOKE_BASELINE is five whole numbers'
    unset BLIT_SMOKE_BASELINE
    cases=$((cases + 1))
    echo "case baseline: two displays recorded, one back -> smoke failed, exit 1"
fi

if want rollback-fails; then
    up good 0.3.200
    printf '0.3.201 0.3.200\n' >"$work/broken"
    live --rollback-to "$(deb 0.3.200)" 127.0.0.1 "$(deb 0.3.201)"
    expect rollback-fails 4 'smoke failed: installed Blit 0.3.201' 'smoke failed: installed Blit 0.3.200' \
        'rollback failed' "the state before the update was '1 1 1 "
    cases=$((cases + 1))
    echo "case rollback-fails: exit 4, the state before the update reported"
fi

if want update; then
    up good 0.3.200
    : >"$work/broken"
    live --rollback-to "$(deb 0.3.200)" 127.0.0.1 "$(deb 0.3.202)"
    expect update 0 'update verified: Blit 0.3.202' 'smoke passed'
    [ "$(wc -l <"$work/puts")" = 1 ] || fail "update: $(wc -l <"$work/puts") uploads, expected 1"
    cases=$((cases + 1))
    echo "case update: $(grep 'smoke passed' "$work/live.log")"
fi

if want refusals; then
    up good 0.3.200
    live --rollback-to "$(deb 0.3.203)" 127.0.0.1 "$(deb 0.3.202)"
    expect no-sig 2 'the rollback pair: the release.s signature .*blit-release_0.3.203-1_amd64.sig is not beside'
    live --rollback-to "$(deb 0.3.132)" 127.0.0.1 "$(deb 0.3.202)"
    expect floor 2 'older than 0.3.133'
    live --rollback-to "$(deb 0.3.201)" 127.0.0.1 "$(deb 0.3.202)"
    expect not-live 2 'names Blit 0.3.201, but the box runs 0.3.200'
    [ ! -s "$work/puts" ] || fail "refusals: something was uploaded"
    cases=$((cases + 3))
    echo "case no-sig, floor, not-live: refused, exit 2, nothing sent"
fi

echo "UPDATE_LIVE_ROLLBACK_PASS cases=$cases"
