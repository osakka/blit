#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d /tmp/blit-trial-gate.XXXXXX)
trap 'rm -rf "$work"' EXIT HUP INT TERM
tcc -std=c99 -Wall -DBLIT_TRIAL_ROOT=\""$work/trial"\" \
 -DBLIT_TRIAL_DROPIN_DIR=\""$work/dropins"\" \
 -DBLIT_TRIAL_HELPER=\""$work/gate"\" \
 "$root/tools/tests/presentation_trial_gate.c" -o "$work/gate"
"$work/gate"
