#!/bin/sh
# tools/tests/rollback_recovery_gate.sh BLIT_DEB [PREVIOUS_BLIT_DEB]
#
# Gitea #112: the rollback and recovery rehearsal. Release N is BLIT_DEB,
# with blit-compositor of its version beside it; release N-1 is
# PREVIOUS_BLIT_DEB (and its compositor beside it), or, when that is not
# given, fabricated from N as update_pair_gate does it: the same contents
# at version <release>-0, the compositor's Depends pinned to it.
#
# Like update_pair_gate it installs and removes packages with real dpkg, so
# it needs root in a throwaway container: tools/release/build-release.sh
# runs it as root in a fresh build-environment container
# (BLIT_BUILD_ENV_ROOT=1), and it refuses to run anywhere else. The update
# helper is the checkout's src/blit_update_helper.c built with its deployed
# paths (/run/blit/update.sock, /var/lib/blit/updates): the helper release N
# ships, which is the one that has to install N-1 on a rollback.
#
# Release signatures (security review SR-2): the helper is built with a
# gate key directory (UPDATE_KEYS_DIR) holding a throwaway key made here,
# and every request, and every update-live.sh run, carries a signature by it
# over the packages it carries. A real N-1 below the oldest release the
# helper installs (UPDATE_OLDEST_RELEASE, the first that verifies) cannot be
# rehearsed through the helper: a rollback that far is root's own dpkg, and
# the gate says so and stops.
#
# The operator's state is set once on N-1 -- an edited /etc/blit/blit.env
# (BLIT_EXPECT_DESKTOP=0 and a line of the operator's own), the generated
# update token, and a settings document in /var/lib/blit/settings.json --
# and after every case it must be byte for byte what it was, with its owner
# and mode.
#
# Cases, each from a known installed state:
#   update            N-1 pair; the helper installs the N pair        -> both N
#   rollback          N; the helper installs the N-1 pair             -> both N-1
#                     (what tools/deploy/update-live.sh with N-1's
#                     package does while the API is up)
#   rollback-half     N; the helper is handed blit N-1 alone          -> refused, still N
#   local-rollback    N; root's own dpkg --install of the N-1 pair,   -> both N-1
#                     compositor first (the path when the API is down)
#   local-half        N; root's dpkg --install of blit N-1 alone      -> refused when
#                     N-1's blit Breaks every newer blit-compositor
#                     (SR-13, from 0.3.132; a fabricated N-1 does), and
#                     nothing changes; with an older N-1, dpkg takes it
#                     (it does not enforce the compositor's pin on a
#                     downgrade) and apt-get check names the broken pin;
#                     either way the N-1 pair repairs it
#   interrupted       N-1; dpkg --unpack of the N pair (unpacked, not
#                     configured: the state a power cut or a killed dpkg
#                     leaves) and then each of
#                       dpkg --configure -a                           -> both N
#                       the helper given the N pair again             -> both N
#                       the helper given the N-1 pair                 -> both N-1
#   failed-configure  N-1; the helper installs an N whose postinst
#                     fails                                          -> the helper
#                     answers ERROR naming the failing postinst (the end
#                     of dpkg's output, SR-21), dpkg --audit names the half-
#                     configured package; the helper given the N-1 pair
#                     -> both N-1, audit and apt-get check clean
#   damaged-files     N; blit-server deleted, the helper and
#                     blit-compositor truncated                      -> dpkg --verify
#                     names all three; root's dpkg --install of the N pair
#                     restores them and --verify is clean
# Root's own dpkg --install of a pair always names the compositor first: blit
# Breaks every newer blit-compositor (SR-13), so on a rollback blit can only
# be unpacked once the compositor is at its version (the helper orders it so).
# Then, unless BLIT_ROLLBACK_SMOKE=0, four more with tools/deploy/update-live.sh
# run for real against the installed blit-server (the gates' mock DRM
# display, bound to 127.0.0.1) and a stand-in for systemd: see "Smoke
# rehearsal" below. docs/security-review-2026-11.md has the runbook these
# cases rehearse and the gaps they found.
set -eu

fail() { echo "ROLLBACK_RECOVERY_GATE_FAIL $*" >&2; exit 1; }

[ "$#" -eq 1 ] || [ "$#" -eq 2 ] ||
    { echo "usage: $0 BLIT_DEB [PREVIOUS_BLIT_DEB]" >&2; exit 2; }
[ "$(id -u)" = 0 ] || fail "needs root in a throwaway container"
# podman writes /run/.containerenv, docker /.dockerenv. Never on a real host.
[ -f /run/.containerenv ] || [ -f /.dockerenv ] ||
    fail "refusing to install packages outside a container"

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
blit_new=$1
test -f "$blit_new" || fail "no package $blit_new"
version=$(dpkg-deb -f "$blit_new" Version)
comp_new=$(dirname "$blit_new")/blit-compositor_${version}_amd64.deb
test -f "$comp_new" || fail "no $comp_new beside $blit_new"

work=$(mktemp -d "${TMPDIR:-/tmp}/blit-rollback.XXXXXX")
helper=$work/blit-update-helper
socket=/run/blit/update.sock
staging=/var/lib/blit/updates
helper_pid=
supervisor_pid=
helper_supervisor=
cleanup() {
    touch "$work/stop"
    for p in "$helper_pid" "$supervisor_pid" "$helper_supervisor"; do
        [ -n "$p" ] && kill "$p" 2>/dev/null || true
    done
    for f in "$work/server.pid" "$work/helper.pid"; do
        [ -f "$f" ] && kill "$(cat "$f")" 2>/dev/null || true
    done
    if grep -q blit-rollback-rehearsal /usr/bin/systemctl 2>/dev/null; then
        rm -f /usr/bin/systemctl
    fi
    command -v gate_display_cleanup >/dev/null 2>&1 && gate_display_cleanup
    rm -rf "$work"
}
trap cleanup EXIT INT TERM

repack() {   # IN OUT SED-ARGS...: a copy of IN with its control file edited
    in=$1 out=$2
    shift 2
    rm -rf "$work/x"
    dpkg-deb -R "$in" "$work/x"
    sed -i "$@" "$work/x/DEBIAN/control"
    dpkg-deb --build --root-owner-group "$work/x" "$out" >/dev/null
}

if [ "$#" -eq 2 ]; then
    blit_old=$2
    test -f "$blit_old" || fail "no package $blit_old"
    old=$(dpkg-deb -f "$blit_old" Version)
    comp_old=$(dirname "$blit_old")/blit-compositor_${old}_amd64.deb
    test -f "$comp_old" || fail "no $comp_old beside $blit_old"
    dpkg --compare-versions "$old" lt "$version" ||
        fail "the previous release $old is not older than $version"
    oldest=$(sed -n 's/^#define UPDATE_OLDEST_RELEASE "\(.*\)"$/\1/p' "$root/src/blit_update_helper.c")
    [ -n "$oldest" ] || fail "no UPDATE_OLDEST_RELEASE in the helper"
    dpkg --compare-versions "$old" ge "$oldest~" ||
        fail "the previous release $old is older than $oldest, the oldest the helper installs (SR-2): a rollback to it is root's own dpkg, not this rehearsal (docs/security-review-2026-11.md)"
    previous=real
else
    old=${version%-1}-0
    [ "$old" != "$version" ] || fail "version $version does not end in -1"
    blit_old=$work/blit_${old}_amd64.deb
    comp_old=$work/blit-compositor_${old}_amd64.deb
    repack "$blit_new" "$blit_old" -e "s/^Version: .*/Version: $old/" \
        -e "s/blit-compositor (>> [^)]*)/blit-compositor (>> $old)/"
    repack "$comp_new" "$comp_old" -e "s/^Version: .*/Version: $old/" \
        -e "s/blit (= [^)]*)/blit (= $old)/"
    previous=fabricated
fi
grep -q "blit (= $version)" <<EOF || fail "blit-compositor $version does not pin blit (= $version)"
$(dpkg-deb -f "$comp_new" Depends)
EOF

# The helper release N ships, at its deployed paths, but for its release
# keys: the gate's own (SR-2), made here.
mkdir -p /run/blit "$work/keys"
sources=$("$root/tools/build-flags.sh" --sources helper)
# shellcheck disable=SC2086
cc -std=gnu11 -O1 -DUPDATE_KEYS_DIR="\"$work/keys\"" -o "$helper" \
    "$root/src/blit_update_helper.c" $sources || fail "helper did not build"
sign=$work/blit-sign
# shellcheck disable=SC2086
cc -std=gnu11 -O1 -o "$sign" "$root/tools/release/blit-sign.c" $sources ||
    fail "blit-sign did not build"
"$sign" keygen "$work/gate.secret" "$work/keys/gate-1.pub" >/dev/null || fail "keygen"

sha() { sha256sum "$1" | cut -c1-64; }
# signature FILE...: the gate key's signature over what FILE... are, as
# tools/release/sign-release.sh signs a release.
signature() {
    bv= bs= cv= cs=
    for f do
        case "$(dpkg-deb -f "$f" Package)" in
            blit) bv=$(dpkg-deb -f "$f" Version) bs=$(sha "$f") ;;
            blit-compositor) cv=$(dpkg-deb -f "$f" Version) cs=$(sha "$f") ;;
        esac
    done
    if [ -n "$cv" ]; then
        "$sign" sign "$work/gate.secret" "$bv" "$bs" "$cv" "$cs"
    else
        "$sign" sign "$work/gate.secret" "$bv" "$bs"
    fi
}
installed() {   # PACKAGE -> "<status> <version>", or "absent"
    v=$(dpkg-query --show --showformat='${db:Status-Status} ${Version}' "$1" 2>/dev/null || true)
    case "$v" in
        ""|not-installed*|config-files*) echo absent ;;
        "installed "*) echo "${v#installed }" ;;
        *) echo "$v" ;;
    esac
}
state() { echo "blit=$(installed blit) blit-compositor=$(installed blit-compositor)"; }
expect_state() {   # CASE BLIT-VERSION COMPOSITOR-VERSION
    [ "$(state)" = "blit=$2 blit-compositor=$3" ] ||
        fail "$1: expected blit=$2 blit-compositor=$3, got $(state)"
}
apt_clean() {   # CASE
    apt-get check >"$work/check.log" 2>&1 ||
        { cat "$work/check.log" >&2; fail "$1: apt-get check"; }
    [ -z "$(dpkg --audit 2>&1)" ] || { dpkg --audit >&2; fail "$1: dpkg --audit"; }
}

# The operator's state, and the proof it survived.
operator_files="/etc/blit/blit.env /etc/blit/update.token /var/lib/blit/settings.json"
snapshot() {
    for f in $operator_files; do
        test -f "$f" || { echo "$f missing"; continue; }
        echo "$f $(stat -c '%U:%G %a' "$f") $(sha "$f")"
    done
}
kept() {   # CASE
    snapshot >"$work/now"
    cmp -s "$work/operator" "$work/now" ||
        fail "$1: the operator's files changed: $(diff "$work/operator" "$work/now" | tr '\n' ' ')"
}

# FILE SHA [FILE SHA]: staged as the server stages them (mkstemp's
# blit-update-XXXXXX in the staging directory), then the helper asked,
# with the gate key's signature over them; the staged copies must be gone
# after its answer.
ask() {
    request=
    files=
    while [ "$#" -ge 2 ]; do
        staged=$(mktemp "$staging/blit-update-XXXXXX")
        cp "$1" "$staged"
        request="$request $staged $2"
        files="$files $1"
        shift 2
    done
    # shellcheck disable=SC2086
    request="$request SIGNATURE $(signature $files)"
    rm -f "$socket"
    "$helper" >"$work/helper.log" 2>&1 &
    helper_pid=$!
    i=0
    while [ ! -S "$socket" ] && [ "$i" -lt 100 ]; do i=$((i + 1)); sleep 0.05; done
    [ -S "$socket" ] || fail "helper did not listen"
    answer=$(python3 - "$socket" "$request" <<'PY'
import socket, sys
s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
s.connect(sys.argv[1])
s.sendall(('UPDATE%s\n' % sys.argv[2]).encode())
data = b''
while True:
    chunk = s.recv(4096)
    if not chunk:
        break
    data += chunk
print(data.decode('utf-8', 'replace').strip())
PY
)
    # A refusal leaves the helper serving; a success makes it exit.
    kill "$helper_pid" 2>/dev/null || true
    wait "$helper_pid" 2>/dev/null || true
    helper_pid=
    left=$(find "$staging" -maxdepth 1 -name 'blit-update-*')
    [ -z "$left" ] || fail "staged files left after '$answer': $left"
    echo "$answer"
}
ask_pair_new() { ask "$blit_new" "$(sha "$blit_new")" "$comp_new" "$(sha "$comp_new")"; }
ask_pair_old() { ask "$blit_old" "$(sha "$blit_old")" "$comp_old" "$(sha "$comp_old")"; }
expect_ok() {   # CASE ANSWER
    [ "$2" = OK ] || fail "$1: expected OK from the helper, got '$2'"
}

dpkg_install() {   # CASE DEB...
    c=$1
    shift
    dpkg --install "$@" >"$work/dpkg.log" 2>&1 ||
        { cat "$work/dpkg.log" >&2; fail "$c: dpkg --install $*"; }
}
at_old() { dpkg_install "$1" "$comp_old" "$blit_old"; expect_state "$1" "$old" "$old"; }
at_new() { dpkg_install "$1" "$comp_new" "$blit_new"; expect_state "$1" "$version" "$version"; }

# --- N-1, and the operator's state on it ---
dpkg --purge blit-compositor blit >/dev/null 2>&1 || true
rm -rf /etc/blit /var/lib/blit/settings.json
at_old setup
sed -i 's/^BLIT_EXPECT_DESKTOP=.*/BLIT_EXPECT_DESKTOP=0/' /etc/blit/blit.env
printf '%s\n' '# the operator was here' 'BLIT_ROLLBACK_REHEARSAL=kept' >>/etc/blit/blit.env
# A settings document as the API writes it (ADR 0067): root's, 0644 under
# the StateDirectory. The smoke rehearsal below has a real server write one.
printf '%s\n' '{"version":1,"revision":1,"input_devices":{},"pointer":{"shape":"precision","palette":"neon","size_px":32,"shadow":true}}' \
    >/var/lib/blit/settings.json
snapshot >"$work/operator"
grep -q 'missing' "$work/operator" && fail "setup: $(grep missing "$work/operator")"
echo "previous release: $previous $old; release: $version"

# --- update, rollback through the update path ---
expect_ok update "$(ask_pair_new)"
expect_state update "$version" "$version"
apt_clean update
kept update
echo "case update: $(state); apt-get check clean; operator files kept"

expect_ok rollback "$(ask_pair_old)"
expect_state rollback "$old" "$old"
apt_clean rollback
kept rollback
echo "case rollback: the helper installed the $old pair over $version; apt-get check clean; operator files kept"

at_new rollback-half
answer=$(ask "$blit_old" "$(sha "$blit_old")")
case "$answer" in
    "ERROR "*"the update must carry it"*) ;;
    *) fail "rollback-half: expected a refusal, got '$answer'" ;;
esac
expect_state rollback-half "$version" "$version"
kept rollback-half
echo "case rollback-half: refused ($answer); still $version"

# --- rollback without the API: root's own dpkg ---
dpkg_install local-rollback "$comp_old" "$blit_old"
expect_state local-rollback "$old" "$old"
apt_clean local-rollback
kept local-rollback
echo "case local-rollback: dpkg --install of the $old pair; apt-get check clean; operator files kept"

at_new local-half
# SR-13: an N-1 whose blit Breaks every newer compositor must be refused.
case "$(dpkg-deb -f "$blit_old" Breaks)" in
    *"blit-compositor (>> $old)"*) half_refused=1 ;;
    *) half_refused=0 ;;
esac
if dpkg --install "$blit_old" >"$work/dpkg.log" 2>&1; then
    [ "$half_refused" = 0 ] ||
        fail "local-half: blit $old Breaks blit-compositor (>> $old), and dpkg took it under blit-compositor $version"
    expect_state local-half "$old" "$version"
    if apt-get check >"$work/check.log" 2>&1; then
        fail "local-half: blit $old under blit-compositor $version, and apt-get check is clean"
    fi
    grep -q "blit-compositor : Depends: blit (= $version)" "$work/check.log" ||
        { cat "$work/check.log" >&2; fail "local-half: apt-get check did not name the pin"; }
    half="dpkg took blit $old alone; apt-get check names the broken pin"
else
    expect_state local-half "$version" "$version"
    half="dpkg refused blit $old alone"
fi
dpkg_install local-half "$comp_old" "$blit_old"
expect_state local-half "$old" "$old"
apt_clean local-half
kept local-half
echo "case local-half: $half; the $old pair repaired it"

# --- an interrupted update: unpacked, not configured ---
interrupt() {   # CASE: from the N-1 pair, N unpacked over it
    at_old "$1"
    dpkg --unpack "$blit_new" "$comp_new" >"$work/dpkg.log" 2>&1 ||
        { cat "$work/dpkg.log" >&2; fail "$1: dpkg --unpack"; }
    expect_state "$1" "unpacked $version" "unpacked $version"
    [ -n "$(dpkg --audit 2>&1)" ] || fail "$1: dpkg --audit is silent about an unpacked update"
}
interrupt interrupted-configure
dpkg --configure -a >"$work/dpkg.log" 2>&1 ||
    { cat "$work/dpkg.log" >&2; fail "interrupted-configure: dpkg --configure -a"; }
expect_state interrupted-configure "$version" "$version"
apt_clean interrupted-configure
kept interrupted-configure
echo "case interrupted/configure: dpkg --configure -a finished it at $version"

interrupt interrupted-again
expect_ok interrupted-again "$(ask_pair_new)"
expect_state interrupted-again "$version" "$version"
apt_clean interrupted-again
kept interrupted-again
echo "case interrupted/update-again: the helper given the $version pair finished it"

interrupt interrupted-rollback
expect_ok interrupted-rollback "$(ask_pair_old)"
expect_state interrupted-rollback "$old" "$old"
apt_clean interrupted-rollback
kept interrupted-rollback
echo "case interrupted/rollback: the helper given the $old pair rolled it back"

# --- an update whose configure fails ---
rm -rf "$work/x"
dpkg-deb -R "$blit_new" "$work/x"
sed -i '0,/^set -eu$/s//set -eu\necho "rehearsal: this postinst fails" >\&2\nexit 1/' \
    "$work/x/DEBIAN/postinst"
grep -q 'rehearsal: this postinst fails' "$work/x/DEBIAN/postinst" ||
    fail "failed-configure: could not break the postinst"
blit_broken=$work/broken/blit_${version}_amd64.deb
mkdir -p "$work/broken"
dpkg-deb --build --root-owner-group "$work/x" "$blit_broken" >/dev/null
at_old failed-configure
answer=$(ask "$blit_broken" "$(sha "$blit_broken")" "$comp_new" "$(sha "$comp_new")")
case "$answer" in
    "ERROR "*"error processing package blit"*"post-installation script subprocess returned error exit status 1"*) ;;
    *) fail "failed-configure: expected the helper to report the failing postinst (SR-21), got '$answer'" ;;
esac
case "$(installed blit)" in
    "half-configured $version") ;;
    *) fail "failed-configure: expected blit half-configured $version, got $(state)" ;;
esac
dpkg --audit 2>&1 | grep -q blit || fail "failed-configure: dpkg --audit does not name blit"
kept failed-configure
expect_ok failed-configure "$(ask_pair_old)"
expect_state failed-configure "$old" "$old"
apt_clean failed-configure
kept failed-configure
echo "case failed-configure: the helper answered '...$(printf '%s' "$answer" | tail -c 100)'; the $old pair recovered it"

# --- damaged files ---
at_new damaged-files
rm -f /usr/lib/blit/bin/blit-server
: >/usr/lib/blit/bin/blit-update-helper
: >/usr/bin/blit-compositor
verify=$(dpkg --verify blit blit-compositor 2>&1 || true)
for f in /usr/lib/blit/bin/blit-server /usr/lib/blit/bin/blit-update-helper /usr/bin/blit-compositor; do
    echo "$verify" | grep -q " $f\$" || fail "damaged-files: dpkg --verify does not name $f: $verify"
done
dpkg_install damaged-files "$comp_new" "$blit_new"
expect_state damaged-files "$version" "$version"
verify=$(dpkg --verify blit blit-compositor 2>&1 || true)
[ -z "$verify" ] || fail "damaged-files: dpkg --verify after the reinstall: $verify"
mkdir -p "$work/n"
dpkg-deb -x "$blit_new" "$work/n"
cmp -s "$work/n/usr/lib/blit/bin/blit-server" /usr/lib/blit/bin/blit-server ||
    fail "damaged-files: blit-server is not the package's"
apt_clean damaged-files
kept damaged-files
echo "case damaged-files: dpkg --verify named all three; dpkg --install of the $version pair restored them"

cases=8
[ "${BLIT_ROLLBACK_SMOKE:-1}" = 1 ] || {
    echo "ROLLBACK_RECOVERY_GATE_PASS version=$version previous=$old ($previous) cases=$cases smoke=skipped operator_files_kept=1"
    exit 0
}

# --- Smoke rehearsal ---
# tools/deploy/update-live.sh, unchanged, against the installed blit-server:
# the server started from /usr/bin/blit with /etc/blit/blit.env as its
# EnvironmentFile and its packaged paths (the update socket, the staging
# directory, the settings document), on the gates' mock DRM display (one
# 1024x768 output), bound to 127.0.0.1 -- never the packaged 0.0.0.0: the
# container shares the host's network -- and a stand-in for Hibr's login
# screen (there is no Hibr here). Two stand-ins for systemd: a loop that starts the
# server and the helper again when they exit (Restart=; StartLimitBurst is
# not modelled), and /usr/bin/systemctl, whose `restart blit.service` stops
# the server for that loop to start again. The manifests update-live.sh
# checks are written for these packages into a scratch copy of the tree.
#   smoke-fails       N-1 up; update-live.sh installs an N whose server
#                     comes up without its display          -> "smoke failed",
#                     exit 1, the API still up
#   smoke-rollback    update-live.sh with N-1               -> verified, the display
#                     back as it was at N-1 (update-live.sh's own smoke
#                     compares with the broken box, so it may end 1)
#   smoke-update      update-live.sh with N; a setting saved through
#                     the API at N                           -> verified, smoke passed
#   server-dead       update-live.sh installs an N whose server exits at
#                     start                                  -> it fails; update-live.sh
#                     with N-1 is refused (no API to talk to); root's
#                     dpkg --install of the N-1 pair and `systemctl restart
#                     blit` bring the box back as it was, and N-1 reads the
#                     setting N saved
. "$root/tools/tests/gate_port.sh"
GATE_DISPLAY_SOURCE=$root/tools/tests/gate_display.sh
. "$root/tools/tests/gate_display.sh"
gate_display_setup || fail "smoke: no mock display"
port=$(gate_port "${BLIT_ROLLBACK_PORT:-19271}")
base=http://127.0.0.1:$port

[ ! -e /usr/bin/systemctl ] || grep -q blit-rollback-rehearsal /usr/bin/systemctl ||
    fail "smoke: a real systemctl is here; this rehearsal stands in for systemd"
cat >/usr/bin/systemctl <<EOF
#!/bin/sh
# blit-rollback-rehearsal: a stand-in for systemd in a throwaway container.
case "\$*" in
    *restart*blit.service*) kill "\$(cat $work/server.pid 2>/dev/null)" 2>/dev/null || true ;;
esac
exit 0
EOF
chmod 0755 /usr/bin/systemctl

# The login screen's stand-in (there is no Hibr here). The package's
# BLIT_LOGIN_AUTOSTART=1 starts it in the first pane after every start, as
# it starts Hibr's login screen on a real box, and it keeps drawing, so
# update-live.sh sees a pane with a surface and frames advancing.
printf '%s\n' '#!/bin/sh' 'i=0' 'while :; do' '    i=$((i + 1))' \
    '    printf "\rrollback rehearsal %d " "$i"' '    /bin/sleep 0.2' 'done' \
    >"$work/login-standin"
chmod 0755 "$work/login-standin"

serve() {   # blit.service: EnvironmentFile=/etc/blit/blit.env, Restart=
    while [ ! -f "$work/stop" ]; do
        # shellcheck disable=SC2046
        env $GATE_SERVER_ENV $(grep -E '^[A-Za-z_][A-Za-z0-9_]*=' /etc/blit/blit.env) \
            $GATE_DISPLAY_ENV BLIT_BIND_ADDRESS=127.0.0.1 \
            BLIT_LOGIN_COMMAND="$work/login-standin" BLIT_LOGIN_SCRIPT= \
            /usr/bin/blit "$port" >>"$work/server.log" 2>&1 &
        echo $! >"$work/server.pid"
        wait $! || true
        sleep 1
    done
}
helper_serve() {   # blit-update-helper.service: Restart=always
    while [ ! -f "$work/stop" ]; do
        "$helper" >>"$work/helper.log" 2>&1 &
        echo $! >"$work/helper.pid"
        wait $! || true
        sleep 0.2
    done
}
smoke_stop() {
    touch "$work/stop"
    kill "$supervisor_pid" "$helper_supervisor" 2>/dev/null || true
    kill "$(cat "$work/server.pid" 2>/dev/null)" "$(cat "$work/helper.pid" 2>/dev/null)" 2>/dev/null || true
    supervisor_pid=
    helper_supervisor=
    rm -f /usr/bin/systemctl
}

api_token=$(sed -n 's/^BLIT_AUTH_TOKEN=//p' /etc/blit/blit.env)
[ -n "$api_token" ] || fail "smoke: no BLIT_AUTH_TOKEN in blit.env"
api() {   # METHOD PATH [CURL-ARGS...]
    m=$1 p=$2
    shift 2
    curl --silent --fail --max-time 8 -X "$m" -H "Authorization: Bearer $api_token" "$@" "$base$p"
}
wait_api() {   # CASE
    i=0
    until api GET /v1/version >/dev/null 2>&1; do
        i=$((i + 1))
        [ "$i" -lt 150 ] || { tail -30 "$work/server.log" >&2; fail "$1: the API did not come up"; }
        sleep 0.2
    done
}
# update-live.sh's own reading of the box (as update_live_smoke_gate does).
base_url=$base
eval "$(sed -n '/^curl_auth()/,/^}/p' "$root/tools/deploy/update-live.sh")"
eval "$(sed -n '/^smoke_state()/,/^}/p' "$root/tools/deploy/update-live.sh")"

mkdir -p "$work/tree/tools/deploy" "$work/tree/releases"
cp "$root/tools/deploy/update-live.sh" "$work/tree/tools/deploy/"
live() {   # BLIT_DEB -> update-live.sh's exit status; its output in $work/live.log
    # The pair copied to a directory of its own with the gate key's
    # signature beside it (SR-2), as a release's directory has the real one.
    pv=$(dpkg-deb -f "$1" Version)
    d=$(mktemp -d "$work/live.XXXXXX")
    cp "$1" "$(dirname "$1")/blit-compositor_${pv}_amd64.deb" "$d/"
    p=$d/$(basename "$1")
    c=$d/blit-compositor_${pv}_amd64.deb
    signature "$p" "$c" >"$d/blit-release_${pv}_amd64.sig"
    printf 'package: %s\npackage_sha256: %s\ncompositor_package: %s\ncompositor_package_sha256: %s\n' \
        "$(basename "$p")" "$(sha "$p")" "$(basename "$c")" "$(sha "$c")" \
        >"$work/tree/releases/${pv%-*}.yaml"
    if env BLIT_BASE_URL="$base" BLIT_AUTH_TOKEN="$api_token" \
            BLIT_UPDATE_TOKEN_FILE=/etc/blit/update.token \
            sh "$work/tree/tools/deploy/update-live.sh" 127.0.0.1 "$p" >"$work/live.log" 2>&1; then
        echo 0
    else
        echo $?
    fi
}
expect_live() {   # CASE STATUS PATTERN
    if [ "$live_status" != "$2" ] || ! grep -q "$3" "$work/live.log"; then
        cat "$work/live.log" >&2
        tail -20 "$work/server.log" >&2
        fail "$1: expected update-live.sh to end $2 with '$3'; it ended $live_status"
    fi
}

# Two broken releases N, repacked from N: blit-server renamed and a wrapper
# in its place. One comes up without its display (without the
# mock DRM library its device is a plain file); the other exits at start.
broken_server() {   # OUT-DIR WRAPPER-LINE
    rm -rf "$work/x"
    dpkg-deb -R "$blit_new" "$work/x"
    mv "$work/x/usr/lib/blit/bin/blit-server" "$work/x/usr/lib/blit/bin/blit-server.real"
    printf '#!/bin/sh\n%s\n' "$2" >"$work/x/usr/lib/blit/bin/blit-server"
    chmod 0755 "$work/x/usr/lib/blit/bin/blit-server"
    mkdir -p "$1"
    dpkg-deb --build --root-owner-group "$work/x" "$1/blit_${version}_amd64.deb" >/dev/null
    cp "$comp_new" "$1/"
}
broken_server "$work/no-display" \
    'exec env -u LD_PRELOAD /usr/lib/blit/bin/blit-server.real "$@"'
broken_server "$work/dead" 'echo "rehearsal: this server does not start" >&2; exit 1'

at_old smoke
rm -f "$work/stop"
serve &
supervisor_pid=$!
helper_serve &
helper_supervisor=$!
wait_api smoke
# The box at N-1 as update-live.sh reads it: the display up and the login
# stand-in showing in its pane.
i=0
while :; do
    before=$(smoke_state) || before=
    set -- $before
    [ "${1:-}" = 1 ] && [ "${2:-}" = 1 ] && [ "${3:-0}" -ge 1 ] && break
    i=$((i + 1))
    [ "$i" -lt 60 ] || { tail -30 "$work/server.log" >&2; fail "smoke: the box at $old is not up: '$before'"; }
    sleep 0.5
done

live_status=$(live "$work/no-display/blit_${version}_amd64.deb")
expect_live smoke-fails 1 'smoke failed'
expect_state smoke-fails "$version" "$version"
api GET /v1/version >/dev/null || fail "smoke-fails: the API is not up after the failed smoke"
kept smoke-fails
echo "case smoke-fails: update-live.sh: $(grep -m1 -o 'smoke failed: [^,]*' "$work/live.log")"

live_status=$(live "$blit_old")
# update-live.sh compares with the box as it found it -- here the broken
# one, with no display -- so a rollback that brings the display back can
# end "smoke failed" (the box is not as it was: it is better). Either end
# is taken; what must hold is the install, and the box as it was at N-1.
grep -q "update verified: Blit ${old%-*}" "$work/live.log" ||
    { cat "$work/live.log" >&2; fail "smoke-rollback: update-live.sh did not verify $old"; }
expect_state smoke-rollback "$old" "$old"
apt_clean smoke-rollback
kept smoke-rollback
back() {   # CASE: the box's displays as they were at N-1, within 30 s
    i=0
    while :; do
        now=$(smoke_state) || now=
        [ "$(echo "$now" | cut -d' ' -f1-2)" = "$(echo "$before" | cut -d' ' -f1-2)" ] && break
        i=$((i + 1))
        [ "$i" -lt 60 ] || fail "$1: the box did not come back: '$now' (before $before)"
        sleep 0.5
    done
}
back smoke-rollback
if [ "$live_status" = 0 ]; then
    echo "case smoke-rollback: update-live.sh with $old: $(grep 'smoke passed' "$work/live.log")"
else
    echo "case smoke-rollback: update-live.sh with $old verified it and ended $live_status: $(grep -o 'smoke failed: [^,]*' "$work/live.log" | head -1); the display is back ($now)"
fi

live_status=$(live "$blit_new")
expect_live smoke-update 0 'smoke passed'
expect_state smoke-update "$version" "$version"
apt_clean smoke-update
kept smoke-update
# A setting saved through the API at N, which N-1 must read after the
# rollback below (ADR 0067: the settings document outlives a release).
api GET /v1/settings >"$work/settings.json" || fail "smoke-update: GET /v1/settings"
python3 -c '
import json, sys
s = json.load(open(sys.argv[1]))
doc = s.get("document") or {}
doc["pointer"] = {"shape": "hand", "palette": "amber", "size_px": 40, "shadow": False}
json.dump({"document": doc}, open(sys.argv[2], "w"))
open(sys.argv[3], "w").write(str(s["revision"]))
' "$work/settings.json" "$work/put.json" "$work/revision" || fail "smoke-update: no settings document"
api PUT /v1/settings -H "If-Match: $(cat "$work/revision")" -H 'Content-Type: application/json' \
    --data-binary "@$work/put.json" >/dev/null || fail "smoke-update: PUT /v1/settings"
i=0
until grep -q '"hand"' /var/lib/blit/settings.json 2>/dev/null; do
    i=$((i + 1))
    [ "$i" -lt 50 ] || fail "smoke-update: the setting was not saved"
    sleep 0.1
done
snapshot >"$work/operator"   # the settings document is the server's own from here
echo "case smoke-update: update-live.sh with $version: $(grep 'smoke passed' "$work/live.log"); a pointer setting saved at $version"

live_status=$(live "$work/dead/blit_${version}_amd64.deb")
expect_live server-dead 1 'update failed: timed out'
expect_state server-dead "$version" "$version"
kept server-dead
live_status=$(live "$blit_old")
expect_live server-dead 1 'update refused: could not read'
dpkg_install server-dead "$comp_old" "$blit_old"
expect_state server-dead "$old" "$old"
apt_clean server-dead
systemctl restart blit.service
wait_api server-dead
back server-dead
api GET /v1/settings >"$work/settings.json" || fail "server-dead: GET /v1/settings at $old"
python3 -c '
import json, sys
s = json.load(open(sys.argv[1]))
assert (s.get("effective") or {}).get("pointer", {}).get("shape") == "hand", s.get("effective", {}).get("pointer")
' "$work/settings.json" || fail "server-dead: $old does not apply the pointer setting $version saved"
kept server-dead
echo "case server-dead: update-live.sh timed out, then was refused for $old (no API); dpkg --install of the $old pair and a restart: the display back, the setting $version saved in effect"
smoke_stop
cases=$((cases + 4))
echo "ROLLBACK_RECOVERY_GATE_PASS version=$version previous=$old ($previous) cases=$cases smoke=passed operator_files_kept=1"
