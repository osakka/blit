#!/bin/sh
set -eu

# The exact Hibr interpreter is the syntax oracle for the Blit-owned launcher.
# This gate also keeps the placement boundary explicit: a future edit must not
# reinstate horizontal projection or reposition the native primary.
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
LAUNCHER=$ROOT/hibr/blit-session.hibr

hibr -n "$LAUNCHER"
grep -Fq 'BLIT_HIBR_PANE_ROW' "$LAUNCHER"
! grep -Fq 'BLIT_HIBR_PRIMARY_ROW' "$LAUNCHER"
! grep -Fq 'hold move ' "$LAUNCHER"
grep -Fq 'hold attach -m -n "$blit_name" "$blit_session"' "$LAUNCHER"
grep -Fq 'hold attach -m "$blit_session"' "$LAUNCHER"

printf '%s\n' 'hibr join placement gate: PASS topology coordinates and native syntax'
