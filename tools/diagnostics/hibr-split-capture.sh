#!/usr/bin/env bash
set -euo pipefail

# Read-only evidence transaction for one Hibr primary in the bottom pane and
# one --join client in the top pane. It never changes topology, sessions,
# input routes, Hibr settings, or authentication state.
script_dir=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
source "$script_dir/http.sh"
base_url=${1:?usage: hibr-split-capture.sh http://host:8080 [seconds] [output-dir]}
duration=${2:-5}
output_dir=${3:-"diagnostics/hibr-split-$(date -u +%Y%m%dT%H%M%SZ)"}

"$script_dir/capture-live.sh" "$base_url" "$duration" "$output_dir" no

mkdir -p "$output_dir/screenshots"
python3 - "$output_dir/displays.json" "$output_dir/display-ids.txt" <<'PY'
import json
import pathlib
import sys

data = json.loads(pathlib.Path(sys.argv[1]).read_text())
ids = []
for item in data.get("displays", data.get("outputs", [])):
    value = str(item.get("id", ""))
    if value:
        ids.append(value if value.startswith("display-") else "display-" + value)
pathlib.Path(sys.argv[2]).write_text("\n".join(ids) + ("\n" if ids else ""))
PY
while IFS= read -r display_id; do
    [ -n "$display_id" ] || continue
    diag_curl --fail --silent --show-error --max-time 30 \
        "$base_url/v1/diagnostics/screenshots/$display_id?cached=1" \
        -D "$output_dir/screenshots/$display_id.bmp.headers" \
        > "$output_dir/screenshots/$display_id.bmp"
    if command -v ffmpeg >/dev/null 2>&1; then
        ffmpeg -hide_banner -loglevel error -y \
            -i "$output_dir/screenshots/$display_id.bmp" \
            "$output_dir/screenshots/$display_id.png"
    fi
done < "$output_dir/display-ids.txt"
sha256sum "$output_dir"/screenshots/*.bmp \
    > "$output_dir/screenshot-sha256.txt" 2>/dev/null || true

python3 - "$output_dir/topology.json" "$output_dir" <<'PY'
import json
import pathlib
import sys

topology = json.loads(pathlib.Path(sys.argv[1]).read_text())
root = pathlib.Path(sys.argv[2])
with (root / "pane-surfaces.txt").open("w", encoding="utf-8") as stream:
    for pane in topology.get("panes", []):
        surface = pane.get("surface_id")
        if surface:
            stream.write(f"{pane['id']} {surface}\n")
PY

python3 "$script_dir/analyze-hibr-split.py" "$output_dir" \
    > "$output_dir/hibr-split-verdict.txt" || status=$?
status=${status:-0}
python3 "$script_dir/validate-capture.py" "$output_dir" \
    > "$output_dir/validation.txt"
exit "$status"
