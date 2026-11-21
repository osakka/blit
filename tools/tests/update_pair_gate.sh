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
#   7. the -0 pair with steamer and blit-gamescope    -> both -1; the retired
#      installed (as a host from before 0.3.75 has     two removed, apt-get
#      them); the pair at -1                            check clean (Gitea #209;
#                                                       dpkg refused it before)
#   8. both at -0; the pair at -1, and the staged      -> both -1, not the
#      files replaced by another blit after the         replacement (SR-10,
#      helper's checks (UPDATE_GATE_AFTER_VERIFY)       Gitea #192)
#   9. both at -0; a blit -1 whose postinst takes      -> ERROR naming the
#      60 s, the helper's dpkg bound set to 5 s         bound, answered within
#                                                       30 s (SR-12, Gitea #194)
# Release signatures (security review SR-2, Gitea #184). The helper reads a
# gate key directory (UPDATE_KEYS_DIR) holding a throwaway key made here;
# every request above is signed with it over what it carries, so each case
# tests what it always tested. Then, each from both at -0:
#  10. the -1 pair, unsigned                           -> refused, nothing changes
#  11. signed with a key not in the directory          -> refused
#  12. signatures for other packages: the -0 pair's,   -> each refused
#      the -1 versions with the -0 hashes, blit -1
#      alone's (for the pair)
#  13. rotation: a second key added to the directory;  -> both -1
#      the pair signed with it
#  14. a pair below the oldest release the helper      -> refused, signed or not
#      installs (UPDATE_OLDEST_RELEASE), signed
# Case 5 (no compositor) is signed over the pair it carries: one signature
# serves a host with the compositor and one without.
# and with case 4, an operator's BLIT_EXPECT_DESKTOP=0 in /etc/blit/blit.env
# is still 0, in one line, after the upgrade (Gitea #180).
# After every answer, installed or refused, the staged files are gone, and
# so are the helper's own copies.
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

work=$(mktemp -d "${TMPDIR:-/tmp}/blit-update-pair.XXXXXX")
sources=$("$root/tools/build-flags.sh" --sources helper)
socket=$work/update.sock
helper=$work/blit-update-helper
private=$work/private
helper_pid=
cleanup() {
    [ -n "$helper_pid" ] && kill "$helper_pid" 2>/dev/null || true
    rm -rf "$work"
}
trap cleanup EXIT INT TERM

# The real helper, with only its directories moved into the work directory.
# Two more builds for cases 8 and 9: one whose gate hook replaces the staged
# files after the checks, one whose dpkg bound is 5 s.
staging=$work/staging
mkdir -p "$staging"
swap=$work/swap/blit_swapped.deb
keys=$work/keys
build_helper() {   # OUT [CC-ARGS...]
    out=$1
    shift
    # shellcheck disable=SC2086
    cc -std=gnu11 -O1 -DUPDATE_SOCKET="\"$socket\"" -DUPDATE_RUNTIME_DIR="\"$work\"" \
        -DUPDATE_STAGING_DIR="\"$staging\"" -DUPDATE_PRIVATE_DIR="\"$private\"" \
        -DUPDATE_KEYS_DIR="\"$keys\"" "$@" \
        -o "$out" "$root/src/blit_update_helper.c" $sources || fail "helper $out did not build"
}
build_helper "$helper"
build_helper "$work/helper-swap" \
    "-DUPDATE_GATE_AFTER_VERIFY=\"for f in $staging/blit-update-*; do cp $swap \\\"\$f\\\"; done\""
build_helper "$work/helper-bound" -DUPDATE_INSTALL_SECONDS=5

# SR-2: throwaway release keys, made here and gone with the work directory.
# The gate key is in the helper's key directory; the other is not, until
# case 13 adds it.
sign=$work/blit-sign
# shellcheck disable=SC2086
cc -std=gnu11 -O1 -o "$sign" "$root/tools/release/blit-sign.c" $sources ||
    fail "blit-sign did not build"
mkdir -p "$keys" "$work/other-keys"
"$sign" keygen "$work/gate.secret" "$keys/gate-1.pub" >/dev/null || fail "keygen (gate)"
"$sign" keygen "$work/other.secret" "$work/other-keys/other-1.pub" >/dev/null || fail "keygen (other)"

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
repack "$blit_new" "$blit_old" -e "s/^Version: .*/Version: $old/" \
    -e "s/blit-compositor (>> [^)]*)/blit-compositor (>> $old)/"
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

# signature SECRET FILE...: the release signature over the statement of
# what FILE... are (each one's Package, Version and SHA-256), as
# tools/release/sign-release.sh makes it.
signature() {
    key=$1
    shift
    bv= bs= cv= cs=
    for f do
        case "$(dpkg-deb -f "$f" Package)" in
            blit) bv=$(dpkg-deb -f "$f" Version) bs=$(sha "$f") ;;
            blit-compositor) cv=$(dpkg-deb -f "$f" Version) cs=$(sha "$f") ;;
        esac
    done
    if [ -n "$cv" ]; then
        "$sign" sign "$key" "$bv" "$bs" "$cv" "$cs"
    else
        "$sign" sign "$key" "$bv" "$bs"
    fi
}

# FILE SHA [FILE SHA]: each file staged as the server stages it, then the
# helper asked; the staged copies must be gone after its answer. The
# request is signed with the gate key over the files it carries, unless
# ASK_SIGNATURE gives the signature (- for none).
ask() {
    request=
    files=
    n=0
    while [ "$#" -ge 2 ]; do
        n=$((n + 1))
        cp "$1" "$staging/blit-update-case$n"
        request="$request $staging/blit-update-case$n $2"
        files="$files $1"
        shift 2
    done
    # shellcheck disable=SC2086
    s=${ASK_SIGNATURE:-$(signature "$work/gate.secret" $files)}
    [ "$s" = - ] || request="$request SIGNATURE $s"
    answer=$(ask_raw $request)
    left=$(find "$staging" "$private" -type f 2>/dev/null)
    [ -z "$left" ] || fail "staged files or copies left after '$answer': $left"
    echo "$answer"
}

ask_raw() {   # REQUEST-ARGS... -> the helper's one-line answer
    rm -f "$socket"
    "${ASK_HELPER:-$helper}" >"$work/helper.log" 2>&1 &
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

sed -i 's/^BLIT_EXPECT_DESKTOP=.*/BLIT_EXPECT_DESKTOP=0/' /etc/blit/blit.env
grep -q '^BLIT_EXPECT_DESKTOP=0$' /etc/blit/blit.env || fail "pair: could not set BLIT_EXPECT_DESKTOP=0"
answer=$(ask "$blit_new" "$(sha "$blit_new")" "$comp_new" "$(sha "$comp_new")")
[ "$answer" = OK ] || fail "pair: expected OK, got '$answer'"
[ "$(grep -c '^BLIT_EXPECT_DESKTOP=' /etc/blit/blit.env)" = 1 ] &&
    grep -q '^BLIT_EXPECT_DESKTOP=0$' /etc/blit/blit.env ||
    fail "pair: the upgrade changed an operator's BLIT_EXPECT_DESKTOP=0: $(grep '^BLIT_EXPECT_DESKTOP' /etc/blit/blit.env | tr '\n' ' ')"
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

# 7. Gitea #209. A host from before 0.3.75 can still have the packages retired
# with the gamescope path: blit-gamescope pinned to its blit, steamer on top
# of it. The -0 pair here is taken without blit's relations to them, as such
# a host's blit had none; the helper then gets the -1 pair.
retired_pkg() {   # NAME DEPENDS -> a minimal package at 0.3.74-1
    mkdir -p "$work/retired/$1/DEBIAN"
    printf 'Package: %s\nVersion: 0.3.74-1\nArchitecture: all\nMaintainer: gate <gate@localhost>\nDepends: %s\nDescription: retired with the gamescope path\n' \
        "$1" "$2" >"$work/retired/$1/DEBIAN/control"
    dpkg-deb --build --root-owner-group "$work/retired/$1" "$work/retired/$1.deb" >/dev/null
}
retired_pkg blit-gamescope "blit (= $old)"
retired_pkg steamer blit-gamescope
repack "$blit_old" "$work/blit_${old}_unrelated.deb" -e '/^Conflicts:/d' -e '/^Replaces:/d'
dpkg --purge blit-compositor blit >/dev/null 2>&1 || true
dpkg --install "$work/blit_${old}_unrelated.deb" "$comp_old" "$work/retired/blit-gamescope.deb" \
    "$work/retired/steamer.deb" >"$work/setup.log" 2>&1 ||
    { cat "$work/setup.log" >&2; fail "could not install the -0 pair with the retired packages"; }
answer=$(ask "$blit_new" "$(sha "$blit_new")" "$comp_new" "$(sha "$comp_new")")
[ "$answer" = OK ] || fail "retired: expected OK, got '$answer'"
[ "$(state)" = "blit=$version blit-compositor=$version" ] || fail "retired: $(state)"
for p in blit-gamescope steamer; do
    [ "$(installed "$p")" = absent ] || fail "retired: $p is still $(installed "$p")"
done
apt-get check >"$work/check.log" 2>&1 || { cat "$work/check.log" >&2; fail "retired: apt-get check"; }
echo "case retired: $(state); steamer and blit-gamescope removed; apt-get check clean"

# 8. SR-10. The helper checks and installs its own copy, so replacing the
# staged files after the checks changes nothing that is installed.
mkdir -p "$work/swap"
repack "$blit_new" "$swap" -e "s/^Version: .*/Version: ${version}+swapped/"
setup_pair_old
answer=$(ASK_HELPER=$work/helper-swap ask "$blit_new" "$(sha "$blit_new")" "$comp_new" "$(sha "$comp_new")")
[ "$answer" = OK ] || fail "swapped: expected OK, got '$answer'"
[ "$(state)" = "blit=$version blit-compositor=$version" ] ||
    fail "swapped: the replaced staged file was installed: $(state)"
echo "case swapped: $(state); the staged files replaced after the checks were not installed"

# 9. SR-12. dpkg is bounded: a postinst that never finishes in time is
# stopped and reported, and the helper answers. Last: it leaves blit
# half-configured.
rm -rf "$work/x"
dpkg-deb -R "$blit_new" "$work/x"
sed -i '0,/^set -eu$/s//set -eu\nsleep 60/' "$work/x/DEBIAN/postinst"
grep -q '^sleep 60$' "$work/x/DEBIAN/postinst" || fail "bound: could not slow the postinst"
mkdir -p "$work/slow"
dpkg-deb --build --root-owner-group "$work/x" "$work/slow/blit_${version}_amd64.deb" >/dev/null
setup_pair_old
started=$(date +%s)
answer=$(ASK_HELPER=$work/helper-bound ask "$work/slow/blit_${version}_amd64.deb" \
    "$(sha "$work/slow/blit_${version}_amd64.deb")" "$comp_new" "$(sha "$comp_new")")
took=$(( $(date +%s) - started ))
case "$answer" in
    "ERROR dpkg did not finish within 5 s and was stopped") ;;
    *) fail "bound: expected the bound to be reported, got '$answer'" ;;
esac
[ "$took" -lt 30 ] || fail "bound: the helper answered after $took s"
echo "case bound: '$answer' after $took s"
setup_pair_old   # not left half-configured for whoever runs next

# --- SR-2: release signatures ---
pair_new="$blit_new $(sha "$blit_new") $comp_new $(sha "$comp_new")"
# shellcheck disable=SC2086
expect_refused unsigned "the update is not signed" "$(ASK_SIGNATURE=- ask $pair_new)"
# shellcheck disable=SC2086
expect_refused other-key "the release signature does not verify" \
    "$(ASK_SIGNATURE=$(signature "$work/other.secret" "$blit_new" "$comp_new") ask $pair_new)"
# shellcheck disable=SC2086
expect_refused old-pair-signature "the release signature does not verify" \
    "$(ASK_SIGNATURE=$(signature "$work/gate.secret" "$blit_old" "$comp_old") ask $pair_new)"
# shellcheck disable=SC2086
expect_refused other-hashes "the release signature does not verify" \
    "$(ASK_SIGNATURE=$("$sign" sign "$work/gate.secret" "$version" "$(sha "$blit_old")" \
        "$version" "$(sha "$comp_old")") ask $pair_new)"
# shellcheck disable=SC2086
expect_refused blit-alone-signature "the release signature does not verify" \
    "$(ASK_SIGNATURE=$(signature "$work/gate.secret" "$blit_new") ask $pair_new)"

cp "$work/other-keys/other-1.pub" "$keys/"
# shellcheck disable=SC2086
answer=$(ASK_SIGNATURE=$(signature "$work/other.secret" "$blit_new" "$comp_new") ask $pair_new)
[ "$answer" = OK ] || fail "rotation: expected OK under the second key, got '$answer'"
[ "$(state)" = "blit=$version blit-compositor=$version" ] || fail "rotation: $(state)"
echo "case rotation: a second key in the directory; the pair signed with it installed: $(state)"
rm -f "$keys/other-1.pub"

oldest=$(sed -n 's/^#define UPDATE_OLDEST_RELEASE "\(.*\)"$/\1/p' "$root/src/blit_update_helper.c")
[ -n "$oldest" ] || fail "below-oldest: no UPDATE_OLDEST_RELEASE in the helper"
mkdir -p "$work/ancient"
blit_ancient=$work/ancient/blit_0.0.1-1_amd64.deb
comp_ancient=$work/ancient/blit-compositor_0.0.1-1_amd64.deb
repack "$blit_new" "$blit_ancient" -e "s/^Version: .*/Version: 0.0.1-1/" \
    -e "s/blit-compositor (>> [^)]*)/blit-compositor (>> 0.0.1-1)/"
repack "$comp_new" "$comp_ancient" -e "s/^Version: .*/Version: 0.0.1-1/" \
    -e "s/blit (= [^)]*)/blit (= 0.0.1-1)/"
setup_pair_old
expect_refused below-oldest "is older than $oldest" \
    "$(ask "$blit_ancient" "$(sha "$blit_ancient")" "$comp_ancient" "$(sha "$comp_ancient")")"

echo "UPDATE_PAIR_GATE_PASS version=$version cases=16 kept_operator_env=1 signed=1"
