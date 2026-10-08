#!/usr/bin/env bash
set -euo pipefail

package=${1:?usage: package_artifact_gate.sh PATH_TO_DEB}
root=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
command -v dpkg-deb >/dev/null 2>&1
test -f "$package"

version=$(dpkg-deb -f "$package" Version)
release=${version%-1}
expected=$(sed -n '1p' "$root/packaging/VERSION")
test "$release" = "$expected"

manifest="$root/releases/$release.yaml"
test -f "$manifest"
actual_hash=$(sha256sum "$package" | awk '{print $1}')
manifest_hash=$(sed -n 's/^package_sha256: //p' "$manifest")
test "$actual_hash" = "$manifest_hash"

stage=$(mktemp -d /tmp/blit-package-gate.XXXXXX)
cleanup() { rm -rf "$stage"; }
trap cleanup EXIT HUP INT TERM
dpkg-deb -x "$package" "$stage"

test -x "$stage/usr/lib/blit/bin/blit-server"
test -f "$stage/usr/lib/blit/hibr/console-blit.so"
test -x "$stage/usr/share/blit/hibr/blit-session.hibr"
test -f "$stage/usr/lib/blit/openapi/blit-0.1.yaml"
strings "$stage/usr/lib/blit/bin/blit-server" | grep -Fx "$release" >/dev/null
cmp -s "$root/build/blit/blit-server" "$stage/usr/lib/blit/bin/blit-server"
cmp -s "$root/openapi/blit-0.1.yaml" "$stage/usr/lib/blit/openapi/blit-0.1.yaml"
for document in README.md roadmap.md verification-status.md diagnostic-capture.md; do
    cmp -s "$root/docs/$document" "$stage/usr/share/doc/blit/docs/$document"
done
if dpkg-deb --contents "$package" | grep -Eq '(/|^)([^ ]*\.sh|start-blit|run-blit|attach-blit)$'; then
    echo "PACKAGE_ARTIFACT_FAIL shell implementation or launcher present" >&2
    exit 1
fi

printf 'PACKAGE_ARTIFACT_PASS version=%s sha256=%s\n' "$version" "$actual_hash"
