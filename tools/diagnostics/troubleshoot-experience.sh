#!/usr/bin/env bash
set -euo pipefail

# Evidence transaction for the physical-box symptom. The timed phase only
# reads small JSON endpoints; framebuffer readback happens after it ends.
source "$(CDPATH= cd -- "$(dirname "$0")" && pwd)/http.sh"
base_url=${1:?usage: troubleshoot-experience.sh http://host:8080 [seconds] [output-dir]}
duration=${2:-15}
output_dir=${3:-"diagnostics/troubleshoot-$(date -u +%Y%m%dT%H%M%SZ)"}

case "$duration" in
    ''|*[!0-9]*) echo 'seconds must be a positive integer' >&2; exit 2 ;;
esac
if [ "$duration" -lt 1 ] || [ "$duration" -gt 300 ]; then
    echo 'seconds must be between 1 and 300' >&2
    exit 2
fi

mkdir -p "$output_dir" "$output_dir/screenshots"
capture_started=$(date -u +%Y-%m-%dT%H:%M:%SZ)
python3 - "$output_dir/capture-metadata.json" "$base_url" "$duration" \
    "$diag_auth_mode" "$capture_started" <<'PY'
import json
import pathlib
import sys

path, base_url, duration, auth_mode, started = sys.argv[1:]
pathlib.Path(path).write_text(json.dumps({
    "capture": "blit-live-diagnostics-v2",
    "started_at": started,
    "base_url": base_url,
    "duration_seconds": int(duration),
    "screenshots": "yes",
    "trace_limit": 128,
    "authentication": auth_mode,
}, indent=2) + "\n", encoding="utf-8")
PY

get() {
    local name=$1 path=$2
    diag_curl --fail --silent --show-error --connect-timeout 3 --max-time 10 \
        "$base_url$path" > "$output_dir/$name"
}

get version.json /v1/version
get server.json /v1/server
get graphics.json /v1/graphics
get audio.json /v1/audio
get displays.json /v1/displays
get topology.json /v1/topology
get input-devices.json /v1/input/devices
get input-routes.json /v1/input/routes
get fonts.json /v1/fonts
get settings.json /v1/settings
get diagnostics-start.json /v1/diagnostics
get runtime-start.json /v1/diagnostics/runtime
get trace-start.json '/v1/diagnostics/trace?limit=128'
for kind in pointer_motion pointer_button pointer_wheel keyboard_key surface_update render_path graphics_payload; do
    get "trace-$kind-start.json" "/v1/diagnostics/trace?kind=$kind&limit=128"
done

python3 - "$output_dir/displays.json" "$output_dir/display-ids.txt" <<'PY'
import json, sys
data = json.load(open(sys.argv[1], encoding='utf-8'))
ids = []
for item in data.get('displays', data.get('outputs', [])):
    value = str(item.get('id', ''))
    if value:
        ids.append(value if value.startswith('display-') else 'display-' + value)
open(sys.argv[2], 'w', encoding='utf-8').write('\n'.join(ids) + ('\n' if ids else ''))
PY

printf '%s\n' "Move the real mouse, click, and reproduce the artifact now; recording ${duration}s." >&2
start_ns=$(date +%s%N)
end_ns=$((start_ns + duration * 1000000000))
while [ "$(date +%s%N)" -lt "$end_ns" ]; do
    stamp=$(date -u +%Y-%m-%dT%H:%M:%S.%3NZ)
    if diag_curl --fail --silent --show-error --connect-timeout 3 --max-time 5 \
        "$base_url/v1/diagnostics/runtime" |
        python3 -c 'import json,sys; d=json.load(sys.stdin); print(json.dumps({"captured_at":"'"$stamp"'", "runtime":d}, separators=(",", ":")))' \
        >> "$output_dir/runtime-samples.jsonl"; then
        :
    else
        printf '%s\n' "{\"captured_at\":\"$stamp\",\"error\":\"runtime_unavailable\"}" \
            >> "$output_dir/runtime-samples.jsonl"
    fi
    sleep 0.25
done

get runtime-end.json /v1/diagnostics/runtime
get trace-end.json '/v1/diagnostics/trace?limit=128'
for kind in pointer_motion pointer_button pointer_wheel keyboard_key surface_update render_path graphics_payload; do
    get "trace-$kind-end.json" "/v1/diagnostics/trace?kind=$kind&limit=128"
done

while IFS= read -r display_id; do
    [ -n "$display_id" ] || continue
    diag_curl --fail --silent --show-error --connect-timeout 3 --max-time 30 \
        "$base_url/v1/diagnostics/screenshots/$display_id?cached=1" \
        -D "$output_dir/screenshots/$display_id.bmp.headers" \
        > "$output_dir/screenshots/$display_id.bmp"
    # BMP is the lossless API contract. Emit a portable inspection copy when
    # ffmpeg is available; keep the raw capture authoritative.
    if command -v ffmpeg >/dev/null 2>&1; then
        ffmpeg -hide_banner -loglevel error -y \
            -i "$output_dir/screenshots/$display_id.bmp" \
            "$output_dir/screenshots/$display_id.png"
    fi
done < "$output_dir/display-ids.txt"
sha256sum "$output_dir"/screenshots/*.bmp > "$output_dir/screenshot-sha256.txt" 2>/dev/null || true
sha256sum "$output_dir"/screenshots/*.png > "$output_dir/screenshot-png-sha256.txt" 2>/dev/null || true

capture_finished=$(date -u +%Y-%m-%dT%H:%M:%SZ)
polls=$(wc -l < "$output_dir/runtime-samples.jsonl" | tr -d ' ')
python3 - "$output_dir/capture-complete.json" "$polls" "$capture_finished" <<'PY'
import json
import pathlib
import sys

path, polls, finished = sys.argv[1:]
pathlib.Path(path).write_text(json.dumps({
    "finished_at": finished,
    "polls": int(polls),
    "status": "complete",
}, indent=2) + "\n", encoding="utf-8")
PY

python3 tools/diagnostics/analyze-troubleshoot.py "$output_dir" \
    | tee "$output_dir/verdict.txt"
python3 tools/diagnostics/validate-capture.py "$output_dir" \
    > "$output_dir/validation.txt"
printf 'capture_dir=%s\n' "$output_dir"
