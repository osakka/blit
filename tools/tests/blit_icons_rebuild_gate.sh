#!/bin/sh
# Gitea #118: BlitIcons-Regular.ttf is rebuilt from its SVGs and catalogue
# with the pinned fontTools (tools/fonts/fonttools.sh) and must be the
# committed file byte for byte -- the shipped icon font is what its sources
# make, nothing hand-kept. 77 when the pinned fontTools cannot be had here.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT"
P=$(tools/fonts/fonttools.sh 2>/dev/null) || { echo "BLIT_ICONS_REBUILD_SKIP the pinned fontTools 4.60.1 is not here and cannot be fetched"; exit 77; }
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
PYTHONPATH=$P python3 tools/fonts/build-blit-icons.py "$T/BlitIcons-Regular.ttf" >"$T/build.out" 2>&1 ||
    { echo "BLIT_ICONS_REBUILD_FAIL the build failed: $(tail -1 "$T/build.out")"; exit 1; }
want=$(sha256sum assets/fonts/BlitIcons-Regular.ttf | cut -d' ' -f1)
got=$(sha256sum "$T/BlitIcons-Regular.ttf" | cut -d' ' -f1)
[ "$got" = "$want" ] || { echo "BLIT_ICONS_REBUILD_FAIL rebuilt $got, committed $want: rebuild and commit, or fix the drawings"; exit 1; }
echo "BLIT_ICONS_REBUILD_PASS sha256=$got fontTools=4.60.1 byte-identical"
