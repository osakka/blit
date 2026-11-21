#!/bin/sh
# tools/tests/release_signature_gate.sh
#
# Security review SR-2 (Gitea #184): the release signature, light. No root,
# no package is installed, no port.
#   1. vendor/monocypher is the pinned 4.0.3 (vendor/monocypher/source.json).
#   2. The verifier (release_signature_gate.c, built with tcc as the helper
#      is): RFC 8032's Ed25519 vectors (key, signature, verification); a
#      flipped bit in the signature or the message, another key, S + L,
#      and a truncated message are refused; the statement byte for byte;
#      a signature for another version, hash, package set or order is
#      refused; a truncated or malformed signature is refused; rotation
#      (two keys, either verifies its own releases); what is not a key (a
#      placeholder, a secret key named .pub, a link, a file others may
#      write) is skipped.
#   3. tools/release/blit-sign: keygen writes the secret 0600 and refuses
#      to overwrite; sign refuses a secret others can read; what it signs
#      verifies, and not for another version.
#   4. The update helper itself, with its dpkg install replaced by a gate
#      hook that fails (UPDATE_GATE_AFTER_VERIFY, so nothing is installed
#      and no dpkg database is written): an unsigned request, a signature
#      by another key and one for other packages are refused with their
#      reasons, and so is a truncated or malformed signature (its staged
#      files removed all the same); a pair below the oldest release it installs is refused even
#      signed; the signed pair passes every check and reaches the hook.
set -eu

fail() { echo "RELEASE_SIGNATURE_GATE_FAIL $*" >&2; exit 1; }

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
[ "$(id -u)" != 0 ] || fail "run it as a user: nothing here needs root"
for tool in tcc cc dpkg-deb sha256sum python3; do
    command -v "$tool" >/dev/null 2>&1 || fail "$tool is required"
done
work=$(mktemp -d "${TMPDIR:-/tmp}/blit-release-signature.XXXXXX")
helper_pid=
cleanup() {
    [ -n "$helper_pid" ] && kill "$helper_pid" 2>/dev/null || true
    rm -rf "$work"
}
trap cleanup EXIT INT TERM
umask 022

# 1. The pinned sources.
vendor=$root/vendor/monocypher
grep -q '"version": "4.0.3"' "$vendor/source.json" || fail "vendor/monocypher is not 4.0.3"
pinned=0
for file in LICENCE.md monocypher.c monocypher.h monocypher-ed25519.c monocypher-ed25519.h; do
    want=$(sed -n "s/^ *\"$file\": \"\([0-9a-f]\{64\}\)\".*/\1/p" "$vendor/source.json" | head -1)
    [ -n "$want" ] || fail "source.json pins no $file"
    [ "$(sha256sum "$vendor/$file" | cut -c1-64)" = "$want" ] ||
        fail "vendor/monocypher/$file is not the pinned file"
    pinned=$((pinned + 1))
done
[ "$(ls "$vendor" | grep -c '\.[ch]$')" = 4 ] || fail "vendor/monocypher holds sources not pinned"

# 2. The verifier.
# shellcheck disable=SC2046
tcc -O2 -std=c99 -o "$work/unit" "$root/tools/tests/release_signature_gate.c" \
    $("$root/tools/build-flags.sh" --sources helper) || fail "the unit did not build with tcc"
mkdir "$work/unit-work"
unit=$("$work/unit" "$work/unit-work") || fail "the unit failed"
unit_cases=${unit#RELEASE_SIGNATURE_UNIT_PASS cases=}

# 3. blit-sign.
sign=$work/blit-sign
"$root/tools/build-tcc.sh" "$sign" sign >/dev/null || fail "blit-sign did not build"
mkdir "$work/keys" "$work/other"
"$sign" keygen "$work/test.secret" "$work/keys/test-1.pub" >/dev/null || fail "keygen"
[ "$(stat -c %a "$work/test.secret")" = 600 ] || fail "keygen: the secret is not 0600"
grep -q '^blit-release-ed25519-secret [0-9a-f]\{64\}$' "$work/test.secret" || fail "keygen: secret format"
grep -q '^blit-release-ed25519-public [0-9a-f]\{64\}$' "$work/keys/test-1.pub" || fail "keygen: public format"
[ "$("$sign" public "$work/test.secret")" = "$(cat "$work/keys/test-1.pub")" ] ||
    fail "public: not the key keygen wrote"
before=$(sha256sum "$work/test.secret")
if "$sign" keygen "$work/test.secret" "$work/new.pub" 2>/dev/null; then
    fail "keygen overwrote a secret key"
fi
[ "$(sha256sum "$work/test.secret")" = "$before" ] && [ ! -e "$work/new.pub" ] ||
    fail "keygen: a refused keygen changed something"
"$sign" keygen "$work/other.secret" "$work/other/other-1.pub" >/dev/null || fail "keygen (other)"
chmod 0640 "$work/test.secret"
if "$sign" sign "$work/test.secret" 0.3.133-1 "$(printf '%064d' 0)" >/dev/null 2>&1; then
    fail "sign used a secret key its group can read"
fi
chmod 0600 "$work/test.secret"

# Two minimal packages, as the helper reads them (Package, Version).
deb() {   # NAME VERSION -> $work/debs/NAME_VERSION.deb
    d=$work/pkg/$1-$2
    mkdir -p "$d/DEBIAN" "$work/debs"
    printf 'Package: %s\nVersion: %s\nArchitecture: all\nMaintainer: gate <gate@localhost>\nDescription: release_signature_gate fixture\n' \
        "$1" "$2" >"$d/DEBIAN/control"
    dpkg-deb --build --root-owner-group "$d" "$work/debs/$1_$2.deb" >/dev/null
    echo "$work/debs/$1_$2.deb"
}
sha() { sha256sum "$1" | cut -c1-64; }
blit=$(deb blit 0.3.133-1)
comp=$(deb blit-compositor 0.3.133-1)
blit_old=$(deb blit 0.3.132-1)
comp_old=$(deb blit-compositor 0.3.132-1)
signed() {   # SECRET BLIT COMP -> the signature over the pair
    "$sign" sign "$1" "$(dpkg-deb -f "$2" Version)" "$(sha "$2")" \
        "$(dpkg-deb -f "$3" Version)" "$(sha "$3")"
}
sig=$(signed "$work/test.secret" "$blit" "$comp") || fail "sign"
"$sign" verify "$work/keys" "$sig" 0.3.133-1 "$(sha "$blit")" 0.3.133-1 "$(sha "$comp")" \
    >/dev/null || fail "blit-sign verify: its own signature"
if "$sign" verify "$work/keys" "$sig" 0.3.134-1 "$(sha "$blit")" 0.3.134-1 "$(sha "$comp")" \
        >/dev/null 2>&1; then
    fail "blit-sign verify: took the signature for another version"
fi
if "$sign" verify "$work/other" "$sig" 0.3.133-1 "$(sha "$blit")" 0.3.133-1 "$(sha "$comp")" \
        >/dev/null 2>&1; then
    fail "blit-sign verify: another key verified it"
fi

# 4. The helper, its install replaced by a failing hook.
staging=$work/staging
mkdir -p "$staging" "$work/run"
socket=$work/run/update.sock
# shellcheck disable=SC2046
cc -std=gnu11 -O1 -DUPDATE_SOCKET="\"$socket\"" -DUPDATE_RUNTIME_DIR="\"$work/run\"" \
    -DUPDATE_STAGING_DIR="\"$staging\"" -DUPDATE_PRIVATE_DIR="\"$work/private\"" \
    -DUPDATE_KEYS_DIR="\"$work/keys\"" -DUPDATE_GATE_AFTER_VERIFY='"exit 1"' \
    -o "$work/helper" "$root/src/blit_update_helper.c" \
    $("$root/tools/build-flags.sh" --sources helper) || fail "the helper did not build"
ask() {   # SIGNATURE|- BLIT COMP -> the helper's answer
    s=$1
    shift
    request=
    n=0
    for f do
        n=$((n + 1))
        cp "$f" "$staging/blit-update-case$n"
        request="$request $staging/blit-update-case$n $(sha "$f")"
    done
    [ "$s" = - ] || request="$request SIGNATURE $s"
    rm -f "$socket"
    "$work/helper" >"$work/helper.log" 2>&1 &
    helper_pid=$!
    i=0
    while [ ! -S "$socket" ] && [ "$i" -lt 100 ]; do i=$((i + 1)); sleep 0.05; done
    [ -S "$socket" ] || fail "the helper did not listen"
    answer=$(python3 -I - "$socket" "UPDATE$request" <<'PY'
import socket, sys
s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
s.connect(sys.argv[1])
s.sendall((sys.argv[2] + '\n').encode())
data = b''
while True:
    chunk = s.recv(4096)
    if not chunk:
        break
    data += chunk
print(data.decode('utf-8', 'replace').strip())
PY
)
    kill "$helper_pid" 2>/dev/null || true
    wait "$helper_pid" 2>/dev/null || true
    helper_pid=
    [ -z "$(ls "$staging")" ] || fail "staged files left after '$answer'"
    echo "$answer"
}
expect() {   # CASE PATTERN ANSWER
    case "$3" in
        $2) echo "case $1: $3" ;;
        *) fail "$1: expected '$2', got '$3'" ;;
    esac
}
helper_cases=0
h() { helper_cases=$((helper_cases + 1)); expect "$@"; }
h unsigned "ERROR the update is not signed*" "$(ask - "$blit" "$comp")"
h other-key "ERROR the release signature does not verify*" \
    "$(ask "$(signed "$work/other.secret" "$blit" "$comp")" "$blit" "$comp")"
h other-packages "ERROR the release signature does not verify*" \
    "$(ask "$(signed "$work/test.secret" "$blit_old" "$comp_old")" "$blit" "$comp")"
h blit-alone "ERROR the release signature does not verify*" "$(ask "$sig" "$blit")"
h malformed "ERROR the signature is not 128 lowercase hex digits" \
    "$(ask "$(echo "$sig" | tr a-f A-F)" "$blit" "$comp")"
h truncated "ERROR the signature is not 128 lowercase hex digits" \
    "$(ask "$(echo "$sig" | cut -c1-127)" "$blit" "$comp")"
h below-oldest "ERROR blit 0.3.132-1 is older than 0.3.133*" \
    "$(ask "$(signed "$work/test.secret" "$blit_old" "$comp_old")" "$blit_old" "$comp_old")"
h signed "ERROR gate hook failed" "$(ask "$sig" "$blit" "$comp")"
cp "$work/other/other-1.pub" "$work/keys/"
h rotation "ERROR gate hook failed" \
    "$(ask "$(signed "$work/other.secret" "$blit" "$comp")" "$blit" "$comp")"
rm "$work/keys/test-1.pub" "$work/keys/other-1.pub"
h no-key "ERROR no release key in*" "$(ask "$sig" "$blit" "$comp")"

echo "RELEASE_SIGNATURE_GATE_PASS vendor=monocypher-4.0.3 pinned=$pinned unit=$unit_cases helper=$helper_cases"
