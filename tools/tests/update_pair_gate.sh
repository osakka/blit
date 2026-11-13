#!/bin/sh
# tools/tests/update_pair_gate.sh BLIT_DEB
#
# ADR 0232: an update installs blit and blit-compositor together. This gate
# runs the real root update helper against real dpkg. It needs root and a
# throwaway system: tools/release/build-release.sh runs it as root in a
# fresh build-environment container (BLIT_BUILD_ENV_ROOT=1), and it refuses
# to run anywhere else, because it installs and removes packages.
#
# The "previous release" is fabricated from the packages under test: the
# same contents, version <release>-0 (lower than -1), and the compositor's
# Depends pinned to it. So the gate needs no other release's files.
#
# Cases, each from a known installed state:
#   1. both installed at -0; blit alone at -1          -> refused, nothing changes
#   2. both at -0; blit -1 with compositor -0          -> refused (versions differ)
#   3. both at -0; a wrong SHA-256                     -> refused
#   4. both at -0; the pair at -1                      -> both -1, apt-get check clean
#   5. blit -0 only; the pair at -1                    -> blit -1, compositor still absent
#   6. a path outside the staging directory           -> refused, and the file kept
# After every answer, installed or refused, the staged files are gone.
set -eu

fail() { echo "UPDATE_PAIR_GATE_FAIL $*" >&2; exit 1; }

[ "$#" -eq 1 ] || { echo "usage: $0 BLIT_DEB" >&2; exit 2; }
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
old=${version%-1}-0
[ "$old" != "$version" ] || fail "version $version does not end in -1"

work=$(mktemp -d /tmp/blit-update-pair.XXXXXX)
socket=$work/update.sock
helper=$work/blit-update-helper
helper_pid=
cleanup() {
    [ -n "$helper_pid" ] && kill "$helper_pid" 2>/dev/null || true
    rm -rf "$work"
}
trap cleanup EXIT INT TERM

# The real helper, with only its socket moved into the work directory.
staging=$work/staging
mkdir -p "$staging"
cc -std=gnu11 -O1 -DUPDATE_SOCKET="\"$socket\"" -DUPDATE_RUNTIME_DIR="\"$work\"" \
    -DUPDATE_STAGING_DIR="\"$staging\"" \
    -o "$helper" "$root/src/blit_update_helper.c" || fail "helper did not build"

# The fabricated previous release.
repack() {   # IN OUT  (sed program on the control file from stdin args)
    in=$1 out=$2
    shift 2
    rm -rf "$work/x"
    dpkg-deb -R "$in" "$work/x"
    sed -i "$@" "$work/x/DEBIAN/control"
    dpkg-deb --build --root-owner-group "$work/x" "$out" >/dev/null
}
blit_old=$work/blit_${old}_amd64.deb
comp_old=$work/blit-compositor_${old}_amd64.deb
repack "$blit_new" "$blit_old" -e "s/^Version: .*/Version: $old/"
repack "$comp_new" "$comp_old" -e "s/^Version: .*/Version: $old/" \
    -e "s/blit (= [^)]*)/blit (= $old)/"

sha() { sha256sum "$1" | cut -c1-64; }
installed() {   # PACKAGE -> version, or "absent"
    v=$(dpkg-query --show --showformat='${db:Status-Status} ${Version}' "$1" 2>/dev/null || true)
    case "$v" in
        "installed "*) echo "${v#installed }" ;;
        *) echo absent ;;
    esac
}
state() { echo "blit=$(installed blit) blit-compositor=$(installed blit-compositor)"; }

# FILE SHA [FILE SHA]: each file staged as the server stages it, then the
# helper asked; the staged copies must be gone after its answer.
ask() {
    request=
    n=0
    while [ "$#" -ge 2 ]; do
        n=$((n + 1))
        cp "$1" "$staging/blit-update-case$n"
        request="$request $staging/blit-update-case$n $2"
        shift 2
    done
    answer=$(ask_raw $request)
    left=$(ls "$staging")
    [ -z "$left" ] || fail "staged files left after '$answer': $left"
    echo "$answer"
}

ask_raw() {   # REQUEST-ARGS... -> the helper's one-line answer
    rm -f "$socket"
    "$helper" >"$work/helper.log" 2>&1 &
    helper_pid=$!
    i=0
    while [ ! -S "$socket" ] && [ "$i" -lt 100 ]; do i=$((i + 1)); sleep 0.05; done
    [ -S "$socket" ] || fail "helper did not listen"
    answer=$(python3 - "$socket" "$*" <<'PY'
import socket, sys
s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
s.connect(sys.argv[1])
s.sendall(('UPDATE %s\n' % sys.argv[2]).encode())
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
    echo "$answer"
}

setup_pair_old() {
    dpkg --purge blit-compositor blit >/dev/null 2>&1 || true
    dpkg --install "$blit_old" "$comp_old" >"$work/setup.log" 2>&1 ||
        { cat "$work/setup.log" >&2; fail "could not install the -0 pair"; }
    [ "$(state)" = "blit=$old blit-compositor=$old" ] || fail "setup: $(state)"
}

expect_refused() {   # CASE DETAIL-SUBSTRING ANSWER
    case "$3" in
        "ERROR "*"$2"*) ;;
        *) fail "$1: expected a refusal naming '$2', got '$3'" ;;
    esac
    [ "$(state)" = "blit=$old blit-compositor=$old" ] ||
        fail "$1: a refusal changed the system: $(state)"
    echo "case $1: refused ($2)"
}

setup_pair_old
expect_refused blit-alone "the update must carry it" \
    "$(ask "$blit_new" "$(sha "$blit_new")")"

expect_refused versions-differ "versions differ" \
    "$(ask "$blit_new" "$(sha "$blit_new")" "$comp_old" "$(sha "$comp_old")")"

expect_refused bad-sha "SHA-256" \
    "$(ask "$blit_new" 0000000000000000000000000000000000000000000000000000000000000000 \
        "$comp_new" "$(sha "$comp_new")")"

answer=$(ask "$blit_new" "$(sha "$blit_new")" "$comp_new" "$(sha "$comp_new")")
[ "$answer" = OK ] || fail "pair: expected OK, got '$answer'"
[ "$(state)" = "blit=$version blit-compositor=$version" ] || fail "pair: $(state)"
apt-get check >"$work/check.log" 2>&1 || { cat "$work/check.log" >&2; fail "pair: apt-get check"; }
echo "case pair: $(state); apt-get check clean"

dpkg --purge blit-compositor blit >/dev/null 2>&1 || true
dpkg --install "$blit_old" >"$work/setup.log" 2>&1 ||
    { cat "$work/setup.log" >&2; fail "could not install blit -0 alone"; }
answer=$(ask "$blit_new" "$(sha "$blit_new")" "$comp_new" "$(sha "$comp_new")")
[ "$answer" = OK ] || fail "no-compositor: expected OK, got '$answer'"
[ "$(state)" = "blit=$version blit-compositor=absent" ] || fail "no-compositor: $(state)"
echo "case no-compositor: $(state); the compositor was not added"

outside=$work/outside.deb
cp "$blit_new" "$outside"
answer=$(ask_raw "$outside" "$(sha "$outside")")
case "$answer" in
    "ERROR not a staged update package"*) ;;
    *) fail "outside: expected a refusal, got '$answer'" ;;
esac
[ -f "$outside" ] || fail "outside: the helper deleted a file it was not given to stage"
echo "case outside: refused; the file kept"

echo "UPDATE_PAIR_GATE_PASS version=$version cases=6"
