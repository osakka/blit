#!/bin/sh
set -eu
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/gate_port.sh"
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/gate_display.sh"
gate_display_setup

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
binary=${BLIT_GATE_BINARY:-$root/build/blit/blit-server}
port=${BLIT_LOGIN_PORT:-19210}
port=$(gate_port "$port")
token=blit-login-gate
log=${TMPDIR:-/tmp}/blit-login-gate.$$.log
osc_dir=
pid=

if ! sudo -n true >/dev/null 2>&1; then
    printf 'LOGIN_MANAGER_GATE_SKIP root_service=required\n'
    exit 77
fi

cleanup()
{
    gate_display_cleanup
    if [ -n "${pid}" ] && kill -0 "${pid}" 2>/dev/null; then
        sudo -n kill -TERM "${pid}" 2>/dev/null || true
        wait "${pid}" 2>/dev/null || true
    fi
    rm -f "$log"
    if [ -n "$osc_dir" ]; then rm -rf "$osc_dir"; fi
}
trap cleanup EXIT INT TERM

sudo -n env $GATE_SERVER_ENV $GATE_DISPLAY_ENV \
    BLIT_LOGIN_COMMAND=/bin/true BLIT_LOGIN_SCRIPT='' \
    BLIT_LOGIN_AUTOSTART=0 BLIT_AUTH_TOKEN="$token" \
    "$binary" "$port" >"$log" 2>&1 &
pid=$!
ready=0
for attempt in 1 2 3 4 5 6 7 8 9 10; do
    if curl --connect-timeout 1 --max-time 2 --fail --silent \
        -H "Authorization: Bearer $token" \
        "http://127.0.0.1:$port/v1/version" >/dev/null 2>&1; then
        ready=1
        break
    fi
    sleep .2
done
test "$ready" = 1

first=$(curl --fail --silent --show-error \
    -H "Authorization: Bearer $token" -H 'Content-Type: application/json' \
    --data '{}' "http://127.0.0.1:$port/v1/login")
printf '%s\n' "$first" | grep -q '"enabled":true'
printf '%s\n' "$first" | grep -q '"login_manager":true'
first_id=$(printf '%s\n' "$first" | sed -n 's/.*"session":{"id":"\([0-9][0-9]*\)".*/\1/p')
test -n "$first_id"

# /bin/true exits immediately. The manager must not leave a dead login screen
# as a successful running resource; it starts a fresh supervised session.
second_id=
for attempt in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15; do
    state=$(curl --fail --silent -H "Authorization: Bearer $token" \
        "http://127.0.0.1:$port/v1/login")
    second_id=$(printf '%s\n' "$state" | sed -n 's/.*"session":{"id":"\([0-9][0-9]*\)".*/\1/p')
    if [ -n "$second_id" ] && [ "$second_id" != "$first_id" ]; then
        break
    fi
    sleep .2
done
test -n "$second_id"
test "$second_id" != "$first_id"
runtime=$(curl --fail --silent --show-error \
    -H "Authorization: Bearer $token" \
    "http://127.0.0.1:$port/v1/diagnostics/runtime")
printf '%s\n' "$runtime" | grep -q 'login_session_exit'

code=$(curl --silent --show-error -o /dev/null -w '%{http_code}' \
    -H "Authorization: Bearer $token" -X DELETE \
    "http://127.0.0.1:$port/v1/login")
test "$code" = 204
sudo -n kill -TERM "$pid" 2>/dev/null || true
wait "$pid" 2>/dev/null || true
pid=

# Security review SR-6 (Gitea #188): GET /v1/login put the login session's
# object in an 8 KB buffer, and session_json added its fields with no bound.
# A login program that sets its title, an OSC 52 clipboard and an OSC 9
# notification of quotes (each escaped to twice its length: 254 + 8188 +
# 2046 bytes) took the object past 8 KB, and the rest of it was written past
# the end of the buffer, on the stack of the root server. Both answers must
# be whole JSON carrying the three texts whole, and the server still serving.
# Since SR-23 the clipboard keeps only OSC 52's selection;base64 form: the
# longest one Blit holds (4092 base64 characters) is set first, and the
# clipboard of quotes after it is not stored.
osc_dir=$(mktemp -d "${TMPDIR:-/tmp}/blit-login-osc.XXXXXX")
chmod 755 "$osc_dir"
cat >"$osc_dir/login" <<'EOF_LOGIN'
#!/bin/sh
quotes() { printf "%${1}s" '' | tr ' ' '"'; }
printf '\033]2;%s\007' "$(quotes 127)"
printf '\033]52;c;%s\007' "$(printf "%1023s" '' | sed 's/ /QUFB/g')"
printf '\033]52;c;%s\007' "$(quotes 4093)"
printf '\033]9;%s\007' "$(quotes 1023)"
exec sleep 60
EOF_LOGIN
chmod 755 "$osc_dir/login"
port=$(gate_port "${BLIT_LOGIN_PORT:-19210}")
sudo -n env $GATE_SERVER_ENV $GATE_DISPLAY_ENV \
    BLIT_LOGIN_COMMAND="$osc_dir/login" BLIT_LOGIN_SCRIPT='' \
    BLIT_LOGIN_AUTOSTART=0 BLIT_AUTH_TOKEN="$token" \
    "$binary" "$port" >"$log" 2>&1 &
pid=$!
ready=0
for attempt in 1 2 3 4 5 6 7 8 9 10; do
    if curl --connect-timeout 1 --max-time 2 --fail --silent \
        -H "Authorization: Bearer $token" \
        "http://127.0.0.1:$port/v1/version" >/dev/null 2>&1; then
        ready=1
        break
    fi
    sleep .2
done
test "$ready" = 1
curl --fail --silent --show-error -o /dev/null \
    -H "Authorization: Bearer $token" -H 'Content-Type: application/json' \
    --data '{}' "http://127.0.0.1:$port/v1/login"
osc_login=
for attempt in $(seq 1 50); do
    osc_login=$(curl --silent --max-time 5 -H "Authorization: Bearer $token" \
        "http://127.0.0.1:$port/v1/login" || true)
    case $osc_login in *'"notification"'*) break ;; esac
    sleep .1
done
osc_id=$(printf '%s\n' "$osc_login" | sed -n 's/.*"session":{"id":"\([0-9][0-9]*\)".*/\1/p')
osc_session=$(curl --silent --max-time 5 -H "Authorization: Bearer $token" \
    "http://127.0.0.1:$port/v1/terminal-sessions/$osc_id" || true)
if ! printf '%s' "$osc_login" | ruby -rjson -e '
  login = (JSON.parse(STDIN.read) rescue abort("GET /v1/login is not JSON")).fetch("session")
  q = "\""
  abort "title" unless login.fetch("title") == q * 127
  abort "clipboard" unless login.fetch("clipboard") == "c;" + "QUFB" * 1023
  abort "notification" unless login.fetch("notification") == q * 1023
  abort "status" unless login.key?("status") && login.key?("frames")
' || ! printf '%s' "$osc_session" | ruby -rjson -e '
  session = JSON.parse(STDIN.read) rescue abort("GET /v1/terminal-sessions/{id} is not JSON")
  abort "clipboard" unless session.fetch("clipboard") == "c;" + "QUFB" * 1023
' || ! curl --fail --silent --max-time 5 -o /dev/null \
        -H "Authorization: Bearer $token" "http://127.0.0.1:$port/v1/version"; then
    printf 'LOGIN_MANAGER_GATE_FAIL session_json: GET /v1/login with a long title, clipboard and notification is not whole JSON, or the server stopped (login %s bytes, session %s bytes)\n' \
        "${#osc_login}" "${#osc_session}" >&2
    exit 1
fi

printf 'LOGIN_MANAGER_GATE_PASS restart=%s->%s stop_status=%s login_json=%s\n' \
    "$first_id" "$second_id" "$code" "${#osc_login}"
