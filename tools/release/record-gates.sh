#!/bin/sh
set -eu

# Gitea #27: a release manifest's gate results, from tools/run-gates.sh
# outputs. Each output gives one line,
#
#   gates_<suite>: pass=N skip=N fail=0 source=<commit>
#
# (suite names joined with "+" when one run had several), written after the
# machine section, replacing that suite's line from an earlier record. The
# run's source must be the manifest's source_commit, clean, and its verdict
# a pass: gates of other source, or a failed run, are refused here, and
# generate-manifest.sh --check refuses them in a manifest.
#
#   record-gates.sh MANIFEST RUN_GATES_OUTPUT...

fail() {
    echo "record gates: $*" >&2
    exit 1
}

[ "$#" -ge 2 ] || { echo "usage: record-gates.sh MANIFEST RUN_GATES_OUTPUT..." >&2; exit 2; }
manifest=$1
shift
test -f "$manifest" || fail "manifest not found: $manifest"
source_commit=$(sed -n 's/^source_commit: //p' "$manifest" | sed -n '1p')
[ -n "$source_commit" ] || fail "the manifest has no source_commit"
lines=''
for output in "$@"; do
    test -f "$output" || fail "run-gates output not found: $output"
    header=$(sed -n 's/^run-gates: source=//p' "$output" | sed -n '1p')
    [ -n "$header" ] || fail "$output has no 'run-gates: source=' line (a runner before 0.3.125)"
    run_source=${header%% suites=*}
    suites=$(printf '%s' "${header#* suites=}" | tr ' ' '+')
    [ -n "$suites" ] || suites=all
    [ "$run_source" = "$source_commit" ] ||
        fail "$output tested $run_source, the manifest's source is $source_commit"
    verdict=$(tail -1 "$output")
    case $verdict in
        RUN_GATES_PASS\ *) ;;
        *) fail "$output did not pass: $verdict" ;;
    esac
    counts=${verdict#RUN_GATES_PASS }
    lines="$lines
gates_$suites: $counts source=$run_source"
done
temporary="$manifest.tmp.$$"
trap 'rm -f "$temporary"' EXIT HUP INT TERM
cp "$manifest" "$temporary"
printf '%s\n' "$lines" | sed '/^$/d' | while IFS= read -r line; do
    key=${line%%:*}
    grep -v "^$key: " "$temporary" >"$temporary.next" || true
    mv -f "$temporary.next" "$temporary"
    printf '%s\n' "$line" >>"$temporary"
done
mv -f "$temporary" "$manifest"
printf '%s\n' "$lines" | sed '/^$/d' | sed 's/^/recorded /'
