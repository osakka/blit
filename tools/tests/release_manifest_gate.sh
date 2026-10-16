#!/usr/bin/env bash
set -euo pipefail

root=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
generator=$root/tools/release/generate-manifest.sh
manifest=${1:-$root/releases/0.1.495.yaml}
package=${2:-}

if [[ -z "$package" ]]; then
    package=$(find /tmp -maxdepth 3 -type f -name 'blit_0.1.495-1_amd64.deb' -print -quit)
fi
[[ -n "$package" && -f "$package" ]] || {
    echo "release manifest gate: package argument required (or 0.1.495 package in /tmp)" >&2
    exit 77
}

[[ -x "$generator" ]] || {
    echo "release manifest gate: generator is not executable" >&2
    exit 1
}

"$generator" --check "$package" "$manifest"

generated=$(mktemp /tmp/blit-release-manifest.XXXXXX)
trap 'rm -f "$generated"' EXIT HUP INT TERM
source_commit=$(sed -n 's/^source_commit: //p' "$manifest" | sed -n '1p')
documentation_commit=$(sed -n 's/^package_documentation_commit: //p' "$manifest" | sed -n '1p')
BLIT_SOURCE_COMMIT=$source_commit \
BLIT_DOCUMENTATION_COMMIT=$documentation_commit \
    "$generator" "$package" "$generated"

while IFS= read -r line; do
    grep -Fqx "$line" "$manifest"
done <"$generated"

echo "RELEASE_MANIFEST_GENERATOR_PASS fields=9"
