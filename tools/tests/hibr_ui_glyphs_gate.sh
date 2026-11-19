#!/bin/sh
# Gitea #118: assets/fonts/hibr-ui-glyphs.json is the characters Hibr's
# desktop draws (wm/glyphs.hibr), checked against the newest Hibr release in
# a Hibr checkout -- BLIT_HIBR_SOURCE, else ~/hibr -- so a Hibr that starts
# drawing a new character fails here until the snapshot (and with it the icon
# gates' coverage) follows. 77 where there is no Hibr checkout.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT"
src=${BLIT_HIBR_SOURCE:-$HOME/hibr}
[ -d "$src/.git" ] || { echo "HIBR_UI_GLYPHS_SKIP no Hibr checkout at $src (BLIT_HIBR_SOURCE)"; exit 77; }
exec python3 tools/fonts/snapshot-hibr-ui.py --check "$src"
