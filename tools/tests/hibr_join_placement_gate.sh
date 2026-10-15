#!/bin/sh
set -eu

# The exact Hibr interpreter is the syntax oracle for the Blit-owned launcher.
# This gate also keeps the placement boundary explicit: a future edit must not
# silently fall back to Hibr's horizontal-only stock --join behavior.
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
LAUNCHER=$ROOT/hibr/blit-session.hibr

hibr -n "$LAUNCHER"
grep -Fq 'BLIT_HIBR_PANE_ROW' "$LAUNCHER"
grep -Fq 'BLIT_HIBR_PRIMARY_ROW' "$LAUNCHER"
grep -Fq 'hold move "$blit_session" "$blit_primary_client"' "$LAUNCHER"
grep -Fq 'hold attach -m -n "$blit_name" "$blit_session"' "$LAUNCHER"
grep -Fq 'hold attach -m "$blit_session"' "$LAUNCHER"

printf '%s\n' 'hibr join placement gate: PASS topology coordinates and native syntax'
