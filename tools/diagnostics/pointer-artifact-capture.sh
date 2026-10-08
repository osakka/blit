#!/usr/bin/env bash
set -euo pipefail

base_url=${1:?usage: pointer-artifact-capture.sh http://host:8080 [output-dir] [screenshots] [physical|api]}
output_dir=${2:-"diagnostics/pointer-$(date -u +%Y%m%dT%H%M%SZ)"}
screenshots=${3:-no}
mode=${4:-physical}

case "$screenshots" in
    yes|no) ;;
    *) echo "screenshots must be yes or no" >&2; exit 2 ;;
esac
case "$mode" in
    physical|api) ;;
    *) echo "mode must be physical or api" >&2; exit 2 ;;
esac

mkdir -p "$output_dir"
if [ "$screenshots" = yes ]; then
    mkdir -p "$output_dir/screenshots"
fi

get() {
    local name=$1 path=$2
    curl --fail --silent --show-error --max-time 30 \
        "$base_url$path" > "$output_dir/$name"
}

get_optional_session() {
    local name=$1 path=$2 status
    status=$(curl --silent --show-error --max-time 30 \
        -o "$output_dir/$name" -w '%{http_code}' \
        "$base_url$path")
    case "$status" in
        2??) ;;
        404)
            printf '{"available":false,"status":404,"path":"%s"}\n' \
                "$path" > "$output_dir/$name"
            ;;
        *)
            echo "session capture failed: HTTP $status $path" >&2
            return 1
            ;;
    esac
}

capture() {
    local label=$1
    get "runtime-$label.json" /v1/diagnostics/runtime
    get_optional_session "session-$label.json" /v1/terminal-sessions/1
    get "trace-pointer-motion-$label.json" \
        '/v1/diagnostics/trace?kind=pointer_motion'
    get "trace-pointer-button-$label.json" \
        '/v1/diagnostics/trace?kind=pointer_button'
    get "trace-render-path-$label.json" \
        '/v1/diagnostics/trace?kind=render_path'
    get "trace-surface-update-$label.json" \
        '/v1/diagnostics/trace?kind=surface_update'
    if [ "$screenshots" = yes ]; then
        curl --fail --silent --show-error --max-time 30 \
            "$base_url/v1/diagnostics/screenshots/display-1" \
            > "$output_dir/screenshots/$label.bmp"
        sha256sum "$output_dir/screenshots/$label.bmp" \
            >> "$output_dir/screenshot-sha256.txt"
    fi
}

get version.json /v1/version
get displays.json /v1/displays
get topology.json /v1/topology
get input-devices.json /v1/input/devices
get input-routes.json /v1/input/routes
get_optional_session session.json /v1/terminal-sessions/1
capture before

if [ "$mode" = api ]; then
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
else
    echo "Move and click the physical pointer now; capture is running for 5 seconds." >&2
    sleep 5
    capture physical
fi

get runtime-end.json /v1/diagnostics/runtime
get trace-end.json /v1/diagnostics/trace
printf 'capture_dir=%s screenshots=%s mode=%s\n' "$output_dir" "$screenshots" "$mode"
