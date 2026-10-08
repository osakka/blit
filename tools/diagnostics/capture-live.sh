#!/usr/bin/env bash
set -euo pipefail

base_url=${1:?usage: capture-live.sh http://host:8080 [seconds] [output-dir] [screenshots]}
duration=${2:-10}
output_dir=${3:-"diagnostics/live-$(date -u +%Y%m%dT%H%M%SZ)"}
screenshots=${4:-yes}
surface_ids=${BLIT_SURFACE_IDS:-surface-1}
trace_limit=${BLIT_TRACE_LIMIT:-8}

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

get() {
    local name=$1 path=$2 tmp attempt
    tmp="$output_dir/.capture-${name//\//_}.tmp"
    for attempt in 1 2 3; do
        if curl --fail --silent --show-error --connect-timeout 3 --max-time 30 \
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

get version.json /v1/version
get server.json /v1/server
get displays.json /v1/displays
get topology.json /v1/topology
get input-devices.json /v1/input/devices
get input-routes.json /v1/input/routes
get_surface_summaries
get diagnostics-start.json /v1/diagnostics
get runtime-start.json /v1/diagnostics/runtime
get trace-start.json "/v1/diagnostics/trace?limit=$trace_limit"
for kind in pointer_button pointer_motion keyboard_key render_path surface_update; do
    get "trace-${kind}-start.json" "/v1/diagnostics/trace?kind=${kind}&limit=$trace_limit"
done

start=$(date +%s)
index=0
while [ $(( $(date +%s) - start )) -lt "$duration" ]; do
    stamp=$(date -u +%Y%m%dT%H%M%S.%3NZ)
    get "runtime-$stamp.json" /v1/diagnostics/runtime
    get "summary-$stamp.json" "/v1/surfaces/${surface_ids%%,*}/summary"
    if [ "$screenshots" = "yes" ]; then
        curl --fail --silent --show-error --max-time 30 \
            "$base_url/v1/diagnostics/screenshots/display-1" \
            > "$output_dir/screenshots/$stamp.bmp"
        sha256sum "$output_dir/screenshots/$stamp.bmp" \
            >> "$output_dir/screenshot-sha256.txt"
    fi
    index=$((index + 1))
    sleep 1
done

get diagnostics-end.json /v1/diagnostics
get_surface_summaries
get runtime-end.json /v1/diagnostics/runtime
get trace-end.json "/v1/diagnostics/trace?limit=$trace_limit"
for kind in pointer_button pointer_motion keyboard_key render_path surface_update; do
    get "trace-${kind}-end.json" "/v1/diagnostics/trace?kind=${kind}&limit=$trace_limit"
done

printf 'capture_dir=%s polls=%s screenshots=%s trace_limit=%s\n' \
    "$output_dir" "$index" "$screenshots" "$trace_limit"
