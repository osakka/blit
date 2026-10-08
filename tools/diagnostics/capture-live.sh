#!/usr/bin/env bash
set -euo pipefail

base_url=${1:?usage: capture-live.sh http://host:8080 [seconds] [output-dir] [screenshots]}
duration=${2:-10}
output_dir=${3:-"diagnostics/live-$(date -u +%Y%m%dT%H%M%SZ)"}
screenshots=${4:-yes}
surface_ids=${BLIT_SURFACE_IDS:-surface-1}

mkdir -p "$output_dir"
if [ "$screenshots" = "yes" ]; then
    mkdir -p "$output_dir/screenshots"
fi

get() {
    local name=$1 path=$2
    curl --fail --silent --show-error --max-time 30 \
        "$base_url$path" > "$output_dir/$name" || true
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

get version.json /v1/version
get server.json /v1/server
get displays.json /v1/displays
get topology.json /v1/topology
get input-devices.json /v1/input/devices
get input-routes.json /v1/input/routes
get_surface_summaries
get diagnostics-start.json /v1/diagnostics
get runtime-start.json /v1/diagnostics/runtime
get trace-start.json /v1/diagnostics/trace
for kind in pointer_button pointer_motion keyboard_key render_path surface_update; do
    get "trace-${kind}-start.json" "/v1/diagnostics/trace?kind=${kind}"
done

start=$(date +%s)
index=0
while [ $(( $(date +%s) - start )) -lt "$duration" ]; do
    stamp=$(date -u +%Y%m%dT%H%M%S.%3NZ)
    get "runtime-$stamp.json" /v1/diagnostics/runtime
    get "trace-$stamp.json" /v1/diagnostics/trace
    for kind in pointer_button pointer_motion keyboard_key render_path surface_update; do
        get "trace-${kind}-$stamp.json" "/v1/diagnostics/trace?kind=${kind}"
    done
    if [ "$screenshots" = "yes" ]; then
        curl --fail --silent --show-error --max-time 30 \
            "$base_url/v1/diagnostics/screenshots/display-1" \
            > "$output_dir/screenshots/$stamp.bmp" || true
        sha256sum "$output_dir/screenshots/$stamp.bmp" \
            >> "$output_dir/screenshot-sha256.txt" 2>/dev/null || true
    fi
    index=$((index + 1))
    sleep 1
done

get diagnostics-end.json /v1/diagnostics
get_surface_summaries
get runtime-end.json /v1/diagnostics/runtime
get trace-end.json /v1/diagnostics/trace
for kind in pointer_button pointer_motion keyboard_key render_path surface_update; do
    get "trace-${kind}-end.json" "/v1/diagnostics/trace?kind=${kind}"
done

printf 'capture_dir=%s polls=%s screenshots=%s\n' "$output_dir" "$index" "$screenshots"
