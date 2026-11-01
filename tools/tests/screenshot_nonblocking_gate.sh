#!/usr/bin/env bash
set -euo pipefail

# Live diagnostic gate. When BLIT_AUTH_TOKEN is set, both requests carry it as
# a bearer token, so the gate also runs against an installed host; without it
# the server must be one that has no token configured.

base=${1:?usage: screenshot_nonblocking_gate.sh http://host:8080 [display-id]}
display=${2:-display-1}
shot=$(mktemp /tmp/blit-screenshot-gate.XXXXXX.bmp)
version=$(mktemp /tmp/blit-screenshot-gate.XXXXXX.json)
shot_pid=''
auth=()
if [ -n "${BLIT_AUTH_TOKEN:-}" ]; then
    auth=(--header "Authorization: Bearer $BLIT_AUTH_TOKEN")
fi
cleanup() {
    if [ -n "$shot_pid" ]; then
        kill "$shot_pid" 2>/dev/null || true
        wait "$shot_pid" 2>/dev/null || true
    fi
    rm -f "$shot" "$version"
}
trap cleanup EXIT HUP INT TERM

# Keep the reader deliberately slow. The render/input loop must remain able to
# answer a small API request while the framebuffer response is draining.
curl --fail --silent --show-error --max-time 8 --limit-rate 4k \
    ${auth[@]+"${auth[@]}"} "$base/v1/diagnostics/screenshots/$display" > "$shot" &
shot_pid=$!
sleep 0.1
start_ms=$(date +%s%3N)
curl --fail --silent --show-error --connect-timeout 1 --max-time 2 \
    ${auth[@]+"${auth[@]}"} "$base/v1/version" > "$version"
elapsed_ms=$(( $(date +%s%3N) - start_ms ))
wait "$shot_pid" 2>/dev/null || true
shot_pid=''

test -s "$shot"
grep -q 'server_version' "$version"
if [ "$elapsed_ms" -gt 1000 ]; then
    echo "SCREENSHOT_NONBLOCKING_FAIL version_ms=$elapsed_ms" >&2
    exit 1
fi
printf 'SCREENSHOT_NONBLOCKING_PASS version_ms=%s bytes=%s\n' \
    "$elapsed_ms" "$(wc -c < "$shot")"
