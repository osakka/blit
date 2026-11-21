#!/bin/sh
# tools/release/sign-release.sh BLIT_DEB
# tools/release/sign-release.sh --check
#
# Security review SR-2 (Gitea #184): a release's signature. BLIT_DEB is the
# release's blit package, blit-compositor of its version beside it; the
# signature over the pair's statement (src/blit_release_signature.h: each
# package's Version and SHA-256) is written beside them as
# blit-release_<version>_amd64.sig, which tools/deploy/update-live.sh sends
# and the host's update helper verifies. tools/release/build-release.sh runs
# this on the host, after every gate has passed, so only a gated release is
# signed and the key never enters the build environment.
#
# The key is the secret half of a Blit release key: BLIT_RELEASE_KEY, by
# default ~/.config/blit-release/ed25519.secret, made by
# tools/release/keygen. It must be outside this checkout, the caller's, and
# readable by no one else (0600), and its public half must be one of
# packaging/release-keys/*.pub, or what it signs could not be installed.
# --check checks just that (build-release.sh does, before it builds), and
# signs nothing.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
fail() { echo "blit: $*" >&2; exit 1; }
check_only=0
if [ "${1:-}" = --check ]; then
    check_only=1
    shift
fi
if [ "$#" -ne $((1 - check_only)) ]; then
    echo "usage: tools/release/sign-release.sh BLIT_DEB | --check" >&2
    exit 2
fi

key=${BLIT_RELEASE_KEY:-${HOME:?}/.config/blit-release/ed25519.secret}
[ -f "$key" ] || fail "no release key at $key: make one with tools/release/keygen, or set BLIT_RELEASE_KEY (docs/development-workflow.md, \"Release signing\")"
real=$(readlink -f -- "$key")
case "$real/" in
    "$ROOT"/*) fail "the release key $key is inside the checkout; it must never be (it would be one 'git add' from the repository)" ;;
esac
[ "$(stat -c %u -- "$real")" = "$(id -u)" ] || fail "the release key $key is not yours"
case "$(stat -c %a -- "$real")" in
    600|400) ;;
    *) fail "the release key $key may be read by others (mode $(stat -c %a -- "$real")); chmod 0600 it" ;;
esac

sign=$ROOT/build/blit/blit-sign
"$ROOT/tools/build-tcc.sh" "$sign" sign >/dev/null
public=$("$sign" public "$real")
matched=
for pub in "$ROOT"/packaging/release-keys/*.pub; do
    [ -f "$pub" ] || continue
    if [ "$(cat "$pub")" = "$public" ]; then
        matched=$(basename "$pub")
        break
    fi
done
[ -n "$matched" ] || fail "the public half of $key is in no packaging/release-keys/*.pub: a host could not verify what it signs"
if [ "$check_only" = 1 ]; then
    echo "release key: $key, public half packaging/release-keys/$matched"
    exit 0
fi

package=$1
[ -f "$package" ] || fail "no package $package"
version=$(dpkg-deb -f "$package" Version)
[ "$(dpkg-deb -f "$package" Package)" = blit ] || fail "$package is not blit"
compositor=$(dirname "$package")/blit-compositor_${version}_amd64.deb
[ -f "$compositor" ] || fail "no $(basename "$compositor") beside $package"
[ "$(dpkg-deb -f "$compositor" Package)" = blit-compositor ] &&
    [ "$(dpkg-deb -f "$compositor" Version)" = "$version" ] ||
    fail "$compositor is not blit-compositor $version"
sha() { sha256sum "$1" | cut -c1-64; }
set -- "$version" "$(sha "$package")" "$version" "$(sha "$compositor")"
out=$(dirname "$package")/blit-release_${version}_amd64.sig
"$sign" sign "$real" "$@" >"$out.tmp"
signature=$(cat "$out.tmp")
# It verifies under the repository's keys, and under the keys this package
# installs -- the ones that will verify the next release.
"$sign" verify "$ROOT/packaging/release-keys" "$signature" "$@" >/dev/null ||
    { rm -f "$out.tmp"; fail "the signature does not verify under packaging/release-keys"; }
stage=$(mktemp -d "${TMPDIR:-/tmp}/blit-sign.XXXXXX")
trap 'rm -rf "$stage"' EXIT HUP INT TERM
dpkg-deb -x "$package" "$stage"
"$sign" verify "$stage/usr/lib/blit/release-keys" "$signature" "$@" >/dev/null ||
    { rm -f "$out.tmp"; fail "the signature does not verify under the keys $package installs"; }
mv -f "$out.tmp" "$out"
echo "signed blit and blit-compositor $version with packaging/release-keys/$matched: $out"
