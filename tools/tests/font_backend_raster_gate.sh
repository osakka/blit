#!/bin/sh
# The KMS compositor's font raster path (ADR 0184: the only one).
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
# The gate runs twice in the font suite (default and retro outline); each run
# owns its binary so two of them can be in flight at once (ADR 0216).
work=$(mktemp -d "${TMPDIR:-/tmp}/blit-font-kms-raster.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
cflags=$("$root/tools/build-flags.sh" --cflags server)
libs=$("$root/tools/build-flags.sh" --libs server)
# shellcheck disable=SC2086
tcc -O2 -std=c99 $cflags \
 "$root/tools/tests/font_kms_raster_gate.c" "$root/src/blit_font.c" \
 "$root/src/blit_raster.c" $libs \
 -o "$work/font-raster-kms"
"$work/font-raster-kms" "$root/assets/fonts/Blit-Terminus16.psf" \
 "${BLIT_GATE_OUTLINE:-$root/assets/fonts/text/Hack-Regular.ttf}" "$root/assets/fonts/NotoColorEmoji.ttf"
