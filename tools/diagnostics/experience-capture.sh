#!/usr/bin/env bash
set -euo pipefail

# A single operator transaction. The timed phase never requests screenshots;
# framebuffer readback is deliberately a separate, post-timing action.
base_url=${1:?usage: experience-capture.sh http://host:8080 [seconds] [output-dir] [screenshots]}
duration=${2:-10}
output_dir=${3:-"diagnostics/experience-$(date -u +%Y%m%dT%H%M%SZ)"}
screenshots=${4:-yes}

case "$screenshots" in
    yes|no) ;;
    *) echo 'screenshots must be yes or no' >&2; exit 2 ;;
esac

BLIT_TRACE_LIMIT=${BLIT_TRACE_LIMIT:-64} \
    tools/diagnostics/capture-live.sh "$base_url" "$duration" "$output_dir" no

if [ "$screenshots" = yes ]; then
    mkdir -p "$output_dir/screenshots"
    python3 - "$output_dir" <<'PY'
import json, pathlib, sys
root = pathlib.Path(sys.argv[1])
data = json.loads((root / "displays.json").read_text())
ids = []
for display in data.get("displays", data.get("outputs", [])):
    value = display.get("id")
    if value is not None:
        value = str(value)
        ids.append(value if value.startswith("display-") else "display-" + value)
(root / "display-ids.txt").write_text("\n".join(ids) + ("\n" if ids else ""))
PY
    while IFS= read -r display_id; do
        [ -n "$display_id" ] || continue
        curl --fail --silent --show-error --max-time 30 \
            "$base_url/v1/diagnostics/screenshots/$display_id?cached=1" \
            > "$output_dir/screenshots/$display_id.bmp"
    done < "$output_dir/display-ids.txt"
    sha256sum "$output_dir"/screenshots/*.bmp \
        > "$output_dir/screenshot-sha256.txt" 2>/dev/null || true
fi

python3 tools/diagnostics/analyze-experience.py "$output_dir" \
    > "$output_dir/analysis.txt"
