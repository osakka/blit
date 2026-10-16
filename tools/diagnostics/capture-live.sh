#!/usr/bin/env bash
set -euo pipefail

source "$(CDPATH= cd -- "$(dirname "$0")" && pwd)/http.sh"

base_url=${1:?usage: capture-live.sh http://host:8080 [seconds] [output-dir] [screenshots]}
duration=${2:-10}
output_dir=${3:-"diagnostics/live-$(date -u +%Y%m%dT%H%M%SZ)"}
screenshots=${4:-yes}
surface_ids=${BLIT_SURFACE_IDS-}
trace_limit=${BLIT_TRACE_LIMIT:-8}
session_ids=${BLIT_SESSION_IDS-}
display_ids=${BLIT_DISPLAY_IDS-}

case "$trace_limit" in
    ''|*[!0-9]*) echo "invalid BLIT_TRACE_LIMIT: $trace_limit" >&2; exit 2 ;;
esac
if [ "$trace_limit" -lt 1 ] || [ "$trace_limit" -gt 128 ]; then
    echo "BLIT_TRACE_LIMIT must be between 1 and 128" >&2
    exit 2
fi

mkdir -p "$output_dir"
if [ "$screenshots" = "yes" ]; then
    mkdir -p "$output_dir/screenshots"
fi
capture_started=$(date -u +%Y-%m-%dT%H:%M:%SZ)
python3 - "$output_dir/capture-metadata.json" "$base_url" "$duration" \
    "$screenshots" "$trace_limit" "$diag_auth_mode" "$capture_started" <<'PY'
import json
import pathlib
import sys

path, base_url, duration, screenshots, trace_limit, auth_mode, started = sys.argv[1:]
pathlib.Path(path).write_text(json.dumps({
    "capture": "blit-live-diagnostics-v3",
    "started_at": started,
    "base_url": base_url,
    "duration_seconds": int(duration),
    "screenshots": screenshots,
    "trace_limit": int(trace_limit),
    "authentication": auth_mode,
}, indent=2) + "\n", encoding="utf-8")
PY

if [ -z "$surface_ids" ]; then
    topology_probe="$output_dir/.topology-probe.json"
    if ! diag_curl --fail --silent --show-error --connect-timeout 3 --max-time 10 \
        "$base_url/v1/topology" > "$topology_probe"; then
        echo "capture: unable to discover surfaces from /v1/topology; set BLIT_SURFACE_IDS" >&2
        unlink "$topology_probe" 2>/dev/null || true
        exit 1
    fi
    surface_ids=$(python3 - "$topology_probe" <<'PY'
import json
import sys
data = json.load(open(sys.argv[1]))
ids = []
for pane in data.get("panes", []):
    value = pane.get("surface_id")
    if value and value not in ids:
        ids.append(value)
print(",".join(ids))
PY
)
    unlink "$topology_probe"
    # A PTY can be live while topology is temporarily missing surface_id;
    # terminal-session details below are authoritative during that transition.
fi

get() {
    local name=$1 path=$2 tmp attempt
    tmp="$output_dir/.capture-${name//\//_}.tmp"
    for attempt in 1 2 3; do
        if diag_curl --fail --silent --show-error --connect-timeout 3 --max-time 30 \
            "$base_url$path" > "$tmp"; then
            mv "$tmp" "$output_dir/$name"
            return 0
        fi
        sleep 0.2
    done
    echo "capture: failed to read $path after 3 attempts" >&2
    return 1
}

get_surface_summaries() {
    local surface_id file_name
    local -a ids
    IFS=',' read -r -a ids <<< "$surface_ids"
    for surface_id in "${ids[@]}"; do
        [ -n "$surface_id" ] || continue
        file_name=${surface_id//\//_}
        get "summary-${file_name}.json" "/v1/surfaces/${surface_id}/summary"
    done
}

get_session_details() {
    local session_id file_name
    get sessions.json /v1/terminal-sessions
    if [ -n "$session_ids" ]; then
        IFS=',' read -r -a ids <<< "$session_ids"
        for session_id in "${ids[@]}"; do
            case "$session_id" in
                ''|*[!0-9]*) echo "capture: invalid BLIT_SESSION_IDS entry: $session_id" >&2; exit 2 ;;
            esac
            file_name="session-${session_id}.json"
            if diag_curl --fail --silent --connect-timeout 3 --max-time 10 \
                "$base_url/v1/terminal-sessions/$session_id" > "$output_dir/.$file_name.tmp"; then
                mv "$output_dir/.$file_name.tmp" "$output_dir/$file_name"
            else
                unlink "$output_dir/.$file_name.tmp" 2>/dev/null || true
            fi
        done
    else
        python3 - "$output_dir/sessions.json" <<'PY'
import json
import pathlib
import sys

root = pathlib.Path(sys.argv[1]).parent
sessions = json.loads(pathlib.Path(sys.argv[1]).read_text())
for session in sessions.get("sessions", []):
    session_id = str(session["id"])
    (root / f"session-{session_id}.json").write_text(
        json.dumps(session, indent=2) + "\n", encoding="utf-8")
PY
    fi
}

get_display_ids() {
    [ -n "$display_ids" ] && return 0
    display_ids=$(python3 - "$output_dir/displays.json" <<'PY'
import json
import sys
data = json.load(open(sys.argv[1]))
ids = []
for display in data.get("displays", data.get("outputs", [])):
    value = display.get("id")
    if value is not None:
        value = str(value)
        ids.append(value if value.startswith("display-") else "display-" + value)
print(",".join(ids))
PY
)
}

derive_surface_ids_from_sessions() {
    [ -n "$surface_ids" ] && return 0
    surface_ids=$(python3 - "$output_dir" <<'PY'
import json
import pathlib
import sys
root = pathlib.Path(sys.argv[1])
ids = []
for path in sorted(root.glob("session-*.json")):
    try:
        value = json.loads(path.read_text()).get("surface_id")
    except (OSError, ValueError):
        continue
    if value and value not in ids:
        ids.append(str(value))
print(",".join(ids))
PY
)
}

get version.json /v1/version
get server.json /v1/server
get graphics.json /v1/graphics
get audio.json /v1/audio
get displays.json /v1/displays
get_display_ids
get topology.json /v1/topology
get input-devices.json /v1/input/devices
get input-routes.json /v1/input/routes
get fonts.json /v1/fonts
get settings.json /v1/settings
get_session_details
derive_surface_ids_from_sessions
if [ -z "$surface_ids" ]; then
    echo "capture: no live surface IDs found in topology or terminal sessions; set BLIT_SURFACE_IDS" >&2
    exit 1
fi
get_surface_summaries
get diagnostics-start.json /v1/diagnostics
get runtime-start.json /v1/diagnostics/runtime
get trace-start.json "/v1/diagnostics/trace?limit=$trace_limit"
for kind in pointer_button pointer_motion pointer_wheel keyboard_key render_path surface_update graphics_payload; do
    get "trace-${kind}-start.json" "/v1/diagnostics/trace?kind=${kind}&limit=$trace_limit"
done

start=$(date +%s)
index=0
while [ $(( $(date +%s) - start )) -lt "$duration" ]; do
    stamp=$(date -u +%Y%m%dT%H%M%S.%3NZ)
    get "runtime-$stamp.json" /v1/diagnostics/runtime
    get "summary-$stamp.json" "/v1/surfaces/${surface_ids%%,*}/summary"
    if [ "$screenshots" = "yes" ]; then
        IFS=',' read -r -a displays <<< "$display_ids"
        for display_id in "${displays[@]}"; do
            [ -n "$display_id" ] || continue
            screenshot="$output_dir/screenshots/${stamp}-${display_id}.bmp"
            diag_curl --fail --silent --show-error --max-time 30 \
                "$base_url/v1/diagnostics/screenshots/$display_id" \
                -D "$screenshot.headers" > "$screenshot.tmp"
            mv "$screenshot.tmp" "$screenshot"
            sha256sum "$screenshot" >> "$output_dir/screenshot-sha256.txt"
        done
    fi
    index=$((index + 1))
    sleep 1
done

get diagnostics-end.json /v1/diagnostics
get_surface_summaries
get_session_details
get runtime-end.json /v1/diagnostics/runtime
get trace-end.json "/v1/diagnostics/trace?limit=$trace_limit"
for kind in pointer_button pointer_motion pointer_wheel keyboard_key render_path surface_update graphics_payload; do
    get "trace-${kind}-end.json" "/v1/diagnostics/trace?kind=${kind}&limit=$trace_limit"
done

capture_finished=$(date -u +%Y-%m-%dT%H:%M:%SZ)
python3 - "$output_dir/capture-complete.json" "$index" "$capture_finished" <<'PY'
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
python3 "$(CDPATH= cd -- "$(dirname "$0")" && pwd)/validate-capture.py" \
    "$output_dir" > "$output_dir/validation.txt"
printf 'capture_dir=%s polls=%s screenshots=%s trace_limit=%s auth=%s\n' \
    "$output_dir" "$index" "$screenshots" "$trace_limit" "$diag_auth_mode"
