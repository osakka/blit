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

# Gitea #27: recorded gate results -- a pass of this source is kept and
# checked; a failed run, or a run of other or uncommitted source, is refused
# by the recorder, and a tampered result by the check.
recorder=$root/tools/release/record-gates.sh
work=$(mktemp -d /tmp/blit-record-gates.XXXXXX)
trap 'rm -f "$generated"; rm -rf "$work"' EXIT HUP INT TERM
cp "$manifest" "$work/m.yaml"
sed -i '/^gates_/d' "$work/m.yaml"
printf 'run-gates: source=%s suites=all\nRUN_GATES_PASS pass=3 skip=1 fail=0\n' "$source_commit" >"$work/pass"
printf 'run-gates: source=%s+dirty suites=all\nRUN_GATES_PASS pass=3 skip=1 fail=0\n' "$source_commit" >"$work/dirty"
printf 'run-gates: source=%s suites=all\nRUN_GATES_FAIL pass=2 skip=1 fail=1 failed: x\n' "$source_commit" >"$work/failed"
printf 'run-gates: source=0000000000000000000000000000000000000000 suites=all\nRUN_GATES_PASS pass=3 skip=1 fail=0\n' >"$work/other"
"$recorder" "$work/m.yaml" "$work/pass" >/dev/null
grep -qx "gates_all: pass=3 skip=1 fail=0 source=$source_commit" "$work/m.yaml" ||
    { echo "release manifest gate: a passing run was not recorded" >&2; exit 1; }
"$generator" --check "$package" "$work/m.yaml" | grep -q ' gates=1$' ||
    { echo "release manifest gate: a recorded pass did not check" >&2; exit 1; }
for refused in dirty failed other; do
    if "$recorder" "$work/m.yaml" "$work/$refused" >/dev/null 2>&1; then
        echo "release manifest gate: a $refused run was recorded" >&2; exit 1
    fi
done
sed -i 's/^gates_all: pass=3 skip=1 fail=0/gates_all: pass=3 skip=1 fail=1/' "$work/m.yaml"
if "$generator" --check "$package" "$work/m.yaml" >/dev/null 2>&1; then
    echo "release manifest gate: a failed gate result checked" >&2; exit 1
fi

echo "RELEASE_MANIFEST_GENERATOR_PASS fields=9 gates=recorded,dirty+failed+other_source_refused,tampered_refused"
