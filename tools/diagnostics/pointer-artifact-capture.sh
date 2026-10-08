#!/usr/bin/env bash
set -euo pipefail

base_url=${1:?usage: pointer-artifact-capture.sh http://host:8080 [output-dir]}
output_dir=${2:-"diagnostics/pointer-$(date -u +%Y%m%dT%H%M%SZ)"}

mkdir -p "$output_dir/screenshots"

get() {
    local name=$1 path=$2
    curl --fail --silent --show-error --max-time 30 \
        "$base_url$path" > "$output_dir/$name"
}

capture() {
    local label=$1
    get "runtime-$label.json" /v1/diagnostics/runtime
    get "session-$label.json" /v1/terminal-sessions/1
    get "trace-pointer-motion-$label.json" \
        '/v1/diagnostics/trace?kind=pointer_motion'
    get "trace-pointer-button-$label.json" \
        '/v1/diagnostics/trace?kind=pointer_button'
    get "trace-render-path-$label.json" \
        '/v1/diagnostics/trace?kind=render_path'
    get "trace-surface-update-$label.json" \
        '/v1/diagnostics/trace?kind=surface_update'
    curl --fail --silent --show-error --max-time 30 \
        "$base_url/v1/diagnostics/screenshots/display-1" \
        > "$output_dir/screenshots/$label.bmp"
    sha256sum "$output_dir/screenshots/$label.bmp" \
        >> "$output_dir/screenshot-sha256.txt"
}

get version.json /v1/version
get displays.json /v1/displays
get topology.json /v1/topology
get input-devices.json /v1/input/devices
get input-routes.json /v1/input/routes
get session.json /v1/terminal-sessions/1
capture before

for point in '300 300' '900 900' '1500 1500' '1100 1100'; do
    x=${point% *}
    y=${point#* }
    curl --fail --silent --show-error --max-time 5 \
        -X POST "$base_url/v1/input/inject" \
        -H 'Content-Type: application/json' \
        --data "{\"kind\":\"pointer_motion\",\"x\":$x,\"y\":$y}" \
        > "$output_dir/inject-$x-$y.json"
    sleep 0.2
    capture "motion-$x-$y"
done

get runtime-end.json /v1/diagnostics/runtime
get trace-end.json /v1/diagnostics/trace
printf 'capture_dir=%s\n' "$output_dir"
