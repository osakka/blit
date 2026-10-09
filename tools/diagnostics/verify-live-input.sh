#!/usr/bin/env bash
set -euo pipefail

base_url=${1:?usage: verify-live-input.sh http://host:8080 [output-dir]}
output_dir=${2:-"diagnostics/input-probe-$(date -u +%Y%m%dT%H%M%SZ)"}
mkdir -p "$output_dir"

get() {
    local name=$1 path=$2
    curl --fail --silent --show-error --max-time 30 \
        "$base_url$path" > "$output_dir/$name"
}

post() {
    local name=$1 body=$2
    curl --fail --silent --show-error --max-time 30 \
        -X POST -H 'Content-Type: application/json' \
        -d "$body" "$base_url/v1/input/inject" > "$output_dir/$name"
}

get version-before.json /v1/version
get runtime-before.json /v1/diagnostics/runtime
get motion-before.json '/v1/diagnostics/trace?kind=pointer_motion'
get button-before.json '/v1/diagnostics/trace?kind=pointer_button'
get wheel-before.json '/v1/diagnostics/trace?kind=pointer_wheel'

post motion-response.json '{"kind":"pointer_motion","x":1201,"y":901}'
post button-press-response.json '{"kind":"pointer_button","button":0,"pressed":true}'
post button-release-response.json '{"kind":"pointer_button","button":0,"pressed":false}'
post wheel-response.json '{"kind":"pointer_wheel","delta":1}'

get runtime-after.json /v1/diagnostics/runtime
get motion-after.json '/v1/diagnostics/trace?kind=pointer_motion'
get button-after.json '/v1/diagnostics/trace?kind=pointer_button'
get wheel-after.json '/v1/diagnostics/trace?kind=pointer_wheel'
get render-after.json '/v1/diagnostics/trace?kind=render_path'

python3 - "$output_dir" <<'PY'
import json
import pathlib
import sys

root = pathlib.Path(sys.argv[1])
before = json.loads((root / "runtime-before.json").read_text())
after = json.loads((root / "runtime-after.json").read_text())
motion = json.loads((root / "motion-after.json").read_text())
button = json.loads((root / "button-after.json").read_text())
wheel = json.loads((root / "wheel-after.json").read_text())

def value(doc, *keys):
    for key in keys:
        doc = doc[key]
    return doc

before_events = value(before, "performance", "input_events")
after_events = value(after, "performance", "input_events")
motion_events = [e for e in motion.get("events", [])
                 if "device=api-pointer" in e.get("detail", "")]
button_events = [e for e in button.get("events", [])
                 if "button=0" in e.get("detail", "")]
wheel_events = [e for e in wheel.get("events", [])
                if "delta=1" in e.get("detail", "")]
pointer = value(after, "snapshot", "render", "pointer")

checks = {
    "api_motion_trace": bool(motion_events),
    "api_button_trace": len(button_events) >= 2,
    "api_wheel_trace": bool(wheel_events),
    "input_dispatch_count": after_events >= before_events + 4,
    "pointer_position": pointer.get("x") == 1201.0 and pointer.get("y") == 901.0,
    "render_path_trace": bool(json.loads((root / "render-after.json").read_text()).get("events")),
}
for name, ok in checks.items():
    print(("PASS " if ok else "FAIL ") + name)
if not all(checks.values()):
    raise SystemExit(1)
print("LIVE_INPUT_PROBE_PASS version=" +
      json.loads((root / "version-before.json").read_text())["server_version"] +
      " dispatch_ms=" + str(value(after, "performance", "last_input_dispatch_ms")) +
      " frame_rate_hz=" + str(value(after, "performance", "frame_rate_hz")))
PY

printf 'capture_dir=%s\n' "$output_dir"
