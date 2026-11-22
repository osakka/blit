#!/bin/sh
# Security review SR-8 (Gitea #190): a session child run as another user
# gets no root secret. A root server with an API token and an update token
# starts three children, each of which writes its own environment and uid
# to a file of its own:
#   own     no "user"                     -> BLIT_API_TOKEN set (ADR 0055: the
#                                            server's own children still have
#                                            what Hibr's adapter needs)
#   root    "user" naming the server's own account (root) -> as own
#   nobody  "user":"nobody"               -> runs as nobody, and neither
#                                            token's value is anywhere in its
#                                            environment, under any name
# Before the fix the nobody child had BLIT_API_TOKEN, and BLIT_AUTH_TOKEN and
# BLIT_UPDATE_TOKEN inherited from the server's own environment (systemd's
# EnvironmentFile puts them there). Bound to 127.0.0.1: a root server with
# a token would otherwise listen on 0.0.0.0. Values are never printed: the gate
# reports which names were found.
set -eu
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/gate_port.sh"
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/gate_display.sh"

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
binary=${BLIT_GATE_BINARY:-$root/build/blit/blit-server}
token=blit-sr8-api-gate-token
update_token=blit-sr8-update-gate-token
pid=
log=
out=

if ! sudo -n true >/dev/null 2>&1; then
    printf 'SESSION_USER_SECRETS_GATE_SKIP root_service=required\n'
    exit 77
fi
if ! id nobody >/dev/null 2>&1; then
    printf 'SESSION_USER_SECRETS_GATE_SKIP no_nobody_account\n'
    exit 77
fi
gate_display_setup
port=$(gate_port "${BLIT_USER_SECRETS_PORT:-19216}")

cleanup()
{
    gate_display_cleanup
    if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; then
        sudo -n kill -TERM "$pid" 2>/dev/null || true
        wait "$pid" 2>/dev/null || true
    fi
    [ -z "$log" ] || rm -f "$log"
    # The children's files are root's and nobody's in a sticky directory.
    [ -z "$out" ] || sudo -n rm -rf "$out"
}
trap cleanup EXIT INT TERM
fail() { echo "SESSION_USER_SECRETS_GATE_FAIL $*" >&2; tail -20 "$log" >&2; exit 1; }

log=$(mktemp "${TMPDIR:-/tmp}/blit-user-secrets-gate.XXXXXX")
# Where nobody can write: its own directory under /tmp, not a TMPDIR that
# may sit under a home nobody cannot enter.
out=$(mktemp -d /tmp/blit-user-secrets.XXXXXX)
chmod 1777 "$out"

# sudo records its command line, tokens and all, in SUDO_COMMAND: not in
# the server's environment (systemd starts it with none).
sudo -n env -u SUDO_COMMAND $GATE_SERVER_ENV $GATE_DISPLAY_ENV \
    BLIT_BIND_ADDRESS=127.0.0.1 BLIT_LOGIN_AUTOSTART=0 BLIT_EXPECT_DESKTOP=0 \
    BLIT_AUTH_TOKEN="$token" BLIT_UPDATE_TOKEN="$update_token" \
    BLIT_SETTINGS_PATH="$out/settings.json" \
    "$binary" "$port" >"$log" 2>&1 &
pid=$!
ready=0
for attempt in $(seq 50); do
    if curl --connect-timeout 1 --max-time 2 --fail --silent \
        -H "Authorization: Bearer $token" \
        "http://127.0.0.1:$port/v1/version" >/dev/null 2>&1; then
        ready=1
        break
    fi
    sleep .2
done
[ "$ready" = 1 ] || fail "the root server did not come up"

# child CASE [USER]: a session whose child writes `id -u` and its
# environment to $out/CASE.uid and $out/CASE.env, then waits.
child() {
    body=$(ruby -rjson -e '
      name, dir, user = ARGV
      script = %q{id -u >"$1.uid"; env >"$1.part" && mv "$1.part" "$1.env"; exec sleep 10}
      req = { command: "/bin/sh", arguments: ["-c", script, "sh", File.join(dir, name)],
              surface: { width: 80, height: 24 } }
      req[:user] = user unless user.to_s.empty?
      print JSON.generate(req)' "$1" "$out" "${2:-}")
    code=$(curl --silent --show-error -o "$out/$1.response" -w '%{http_code}' \
        -H "Authorization: Bearer $token" -H 'Content-Type: application/json' \
        --data "$body" "http://127.0.0.1:$port/v1/terminal-sessions")
    [ "$code" = 201 ] || fail "$1: session create answered $code: $(cat "$out/$1.response")"
    i=0
    until [ -s "$out/$1.env" ]; do
        i=$((i + 1))
        [ "$i" -lt 50 ] || fail "$1: the child wrote no environment"
        sleep .1
    done
}
# found CASE: the names whose value is either token, never the values.
found() {
    names=
    while IFS= read -r line; do
        case "$line" in
            *"$token"*|*"$update_token"*) names="$names${names:+,}${line%%=*}" ;;
        esac
    done <"$out/$1.env"
    printf '%s\n' "${names:-none}"
}

child own
child root root
child nobody nobody

own=$(found own)
root_found=$(found root)
nobody_found=$(found nobody)
nobody_uid=$(cat "$out/nobody.uid")

[ "$(cat "$out/own.uid")" = 0 ] || fail "own: the child is not root"
case ",$own," in *,BLIT_API_TOKEN,*) ;; *) fail "own: BLIT_API_TOKEN missing (found: $own)" ;; esac
[ "$(cat "$out/root.uid")" = 0 ] || fail "root: the child is not root"
case ",$root_found," in *,BLIT_API_TOKEN,*) ;; *) fail "root: BLIT_API_TOKEN missing (found: $root_found)" ;; esac
[ "$nobody_uid" = "$(id -u nobody)" ] || fail "nobody: the child runs as uid $nobody_uid"
grep -q '^BLIT_SESSION_ID=' "$out/nobody.env" || fail "nobody: no Blit binding at all (BLIT_SESSION_ID)"
[ "$nobody_found" = none ] ||
    fail "nobody: a child run as another user holds a root secret, in $nobody_found"
if grep -qF "$token" "$out"/*.response; then fail "a session response carries the API token"; fi

echo "SESSION_USER_SECRETS_GATE_PASS own=$own root=$root_found nobody=$nobody_found nobody_uid=$nobody_uid"
