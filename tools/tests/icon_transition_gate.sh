#!/bin/sh
# ADR 0243: the icons moved from U+100000-U+100495 to U+101000-U+101495, and
# while the transition lasts the old range draws exactly the new one's
# glyphs, in the raster and on both compositors (icon_transition_gate.c).
# Built once as shipped, where it must pass, and once with the mapping
# compiled out (BLIT_ICON_OLD_RANGE_DROPPED), where it must fail: the check
# is the mapping's, not a cache's. When a release drops the old range, this
# gate goes with it.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
bin=${TMPDIR:-/tmp}/blit-icon-transition-$$
trap 'rm -f "$bin" "$bin.dropped" "$bin.out"' EXIT
cd "$ROOT"
# The catalogue's plane-16 glyphs, each of which must draw at its new codepoint.
glyphs=$(python3 -c '
import json, sys
c = json.load(open(sys.argv[1]))
print(sum(int(e["codepoint"], 16) >= 0x101000 for k in ("icons", "text_glyphs", "cell_geometry") for e in c.get(k, [])))
' assets/fonts/icons/catalogue.json)
cflags=$(tools/build-flags.sh --cflags drm font zlib)
libs=$(tools/build-flags.sh --libs drm font zlib)
for mode in shipped dropped; do
    out=$bin
    extra=
    if [ "$mode" = dropped ]; then out=$bin.dropped; extra=-DBLIT_ICON_OLD_RANGE_DROPPED; fi
    # shellcheck disable=SC2086
    tcc -O2 -std=c99 $extra $cflags \
        tools/tests/icon_transition_gate.c src/blit_font.c src/blit_raster.c \
        $libs -o "$out"
done
if BLIT_FONT_ROOT="$ROOT/assets/fonts" "$bin.dropped" "$glyphs" >"$bin.out" 2>&1; then
    echo "ICON_TRANSITION_FAIL the check passes with the mapping compiled out: it proves nothing"
    exit 1
fi
grep -q 'does not draw as' "$bin.out" ||
    { echo "ICON_TRANSITION_FAIL without the mapping it failed for another reason:"; head -3 "$bin.out"; exit 1; }
missed=$(grep -c 'does not draw as\|plane .* is not\|cell tests differ\|frame differs' "$bin.out" || true)
BLIT_FONT_ROOT="$ROOT/assets/fonts" "$bin" "$glyphs" >"$bin.out" 2>&1 || { grep -v '^blit: ' "$bin.out"; exit 1; }
echo "$(grep ICON_TRANSITION_PASS "$bin.out") without_mapping=fails($missed)"
