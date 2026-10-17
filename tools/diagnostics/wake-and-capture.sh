#!/usr/bin/env bash
set -euo pipefail

# Controlled live diagnostic for an idle Hibr login-manager/screen-saver state.
#
# This intentionally performs one small mutation: either a one-pixel absolute
# pointer motion or one complete pointer click through POST /v1/input/inject.
# It uses the same API input path as a client, does not create or destroy
# sessions, and never sends a keyboard key.
# The result is evidence about whether Blit accepted and presented the wake
# event; it is not a claim that Hibr authenticated a desktop.

source "$(CDPATH= cd -- "$(dirname "$0")" && pwd)/http.sh"

if [ "$#" -lt 1 ] || [ "$#" -gt 3 ]; then
    echo "usage: wake-and-capture.sh http://host:8080 [output-dir] [motion|click]" >&2
    exit 2
fi

base_url=$1
output_dir=${2:-"diagnostics/wake-$(date -u +%Y%m%dT%H%M%SZ)"}
action=${3:-motion}
case "$action" in
    motion|click) ;;
    *) echo 'wake probe: action must be motion or click' >&2; exit 2 ;;
esac

mkdir -p "$output_dir/screenshots"

get() {
    local name=$1 path=$2
    diag_curl --fail --silent --show-error --connect-timeout 3 --max-time 30 \
        "$base_url$path" > "$output_dir/$name"
}

discover_displays() {
    python3 - "$output_dir/displays.json" "$output_dir/display-ids.txt" <<'PY'
import json
import sys

data = json.load(open(sys.argv[1], encoding='utf-8'))
ids = []
for item in data.get('displays', data.get('outputs', [])):
    value = str(item.get('id', ''))
    if value:
        ids.append(value if value.startswith('display-') else 'display-' + value)
open(sys.argv[2], 'w', encoding='utf-8').write(
    '\n'.join(ids) + ('\n' if ids else ''))
if not ids:
    raise SystemExit('wake probe: no displays reported')
PY
}

capture_screenshots() {
    local label=$1 display_id screenshot
    while IFS= read -r display_id; do
        [ -n "$display_id" ] || continue
        screenshot="$output_dir/screenshots/$label-$display_id.bmp"
        diag_curl --fail --silent --show-error --connect-timeout 3 --max-time 30 \
            "$base_url/v1/diagnostics/screenshots/$display_id?cached=1" \
            -D "$screenshot.headers" > "$screenshot"
        sha256sum "$screenshot" >> "$output_dir/screenshot-sha256.txt"
    done < "$output_dir/display-ids.txt"
}

get version.json /v1/version
get displays.json /v1/displays
discover_displays
get topology-before.json /v1/topology
get sessions-before.json /v1/terminal-sessions
get runtime-before.json /v1/diagnostics/runtime
get trace-pointer-before.json '/v1/diagnostics/trace?kind=pointer_motion&limit=128'
get trace-button-before.json '/v1/diagnostics/trace?kind=pointer_button&limit=128'
get trace-render-before.json '/v1/diagnostics/trace?kind=render_path&limit=128'
capture_screenshots before

python3 - "$output_dir/runtime-before.json" "$output_dir/topology-before.json" \
    "$output_dir/wake-event.json" "$output_dir/button-down-event.json" \
    "$output_dir/button-up-event.json" <<'PY'
import json
import sys

runtime = json.load(open(sys.argv[1], encoding='utf-8'))
topology = json.load(open(sys.argv[2], encoding='utf-8'))
pointer = runtime.get('snapshot', {}).get('render', {}).get('pointer', {})
if not pointer.get('valid'):
    raise SystemExit('wake probe: Blit did not report a valid pointer')

x = int(round(float(pointer.get('x', 0))))
y = int(round(float(pointer.get('y', 0))))
outputs = topology.get('outputs', [])
if not outputs:
    raise SystemExit('wake probe: topology has no output bounds')
bound = outputs[0].get('bounds', {})
width = int(bound.get('width', 0))
height = int(bound.get('height', 0))
if width < 2 or height < 1:
    raise SystemExit('wake probe: invalid first output bounds')

# Keep the pointer inside the first output and move it by exactly one pixel.
if x < width - 1:
    wake_x = x + 1
    wake_y = max(0, min(y, height - 1))
else:
    wake_x = x - 1
    wake_y = max(0, min(y, height - 1))

json.dump({'kind': 'pointer_motion', 'x': wake_x, 'y': wake_y},
          open(sys.argv[3], 'w', encoding='utf-8'), separators=(',', ':'))
json.dump({'kind': 'pointer_button', 'button': 0, 'pressed': True},
          open(sys.argv[4], 'w', encoding='utf-8'), separators=(',', ':'))
json.dump({'kind': 'pointer_button', 'button': 0, 'pressed': False},
          open(sys.argv[5], 'w', encoding='utf-8'), separators=(',', ':'))
PY

post_event() {
    local event=$1 result=$2
    diag_curl --fail --silent --show-error --connect-timeout 3 --max-time 10 \
        -X POST "$base_url/v1/input/inject" \
        -H 'Content-Type: application/json' \
        -H "Idempotency-Key: blit-wake-$(date +%s%N)-$$-$result" \
        --data-binary "@$output_dir/$event.json" > "$output_dir/$result-response.json"
}

if [ "$action" = motion ]; then
    post_event wake-event wake
else
    post_event button-down-event button-down
    sleep 0.1
    post_event button-up-event button-up
fi

# Give the normal input, Hibr PTY, terminal, and presentation paths a bounded
# opportunity to react without turning this probe into a workload.
sleep 2

get topology-after.json /v1/topology
get sessions-after.json /v1/terminal-sessions
get runtime-after.json /v1/diagnostics/runtime
get trace-pointer-after.json '/v1/diagnostics/trace?kind=pointer_motion&limit=128'
get trace-button-after.json '/v1/diagnostics/trace?kind=pointer_button&limit=128'
get trace-render-after.json '/v1/diagnostics/trace?kind=render_path&limit=128'
capture_screenshots after

python3 - "$output_dir" "$action" <<'PY'
import hashlib
import json
import pathlib
import sys

root = pathlib.Path(sys.argv[1])
action = sys.argv[2]

def read(name):
    return json.loads((root / name).read_text(encoding='utf-8'))

before_runtime = read('runtime-before.json')
after_runtime = read('runtime-after.json')
before_sessions = read('sessions-before.json')
after_sessions = read('sessions-after.json')
before_trace = read('trace-pointer-before.json')
after_trace = read('trace-pointer-after.json')
before_buttons = read('trace-button-before.json')
after_buttons = read('trace-button-after.json')
responses = []
for name in ('wake-response.json', 'button-down-response.json',
             'button-up-response.json'):
    path = root / name
    if path.exists():
        responses.append(read(name))

def session_revisions(data):
    return {str(s.get('id')): s.get('surface_revision')
            for s in data.get('sessions', []) if s.get('surface_id')}

before_rev = session_revisions(before_sessions)
after_rev = session_revisions(after_sessions)
revision_changes = {
    key: {'before': before_rev.get(key), 'after': after_rev.get(key)}
    for key in sorted(set(before_rev) | set(after_rev))
    if before_rev.get(key) != after_rev.get(key)
}

def perf(data):
    return data.get('performance', {})

before_perf = perf(before_runtime)
after_perf = perf(after_runtime)
pointer_events = after_trace.get('events', [])
before_pointer_events = before_trace.get('events', [])
button_events = after_buttons.get('events', [])
before_button_events = before_buttons.get('events', [])

before_hash = {}
after_hash = {}
for path in sorted((root / 'screenshots').glob('before-*.bmp')):
    before_hash[path.name.removeprefix('before-')] = hashlib.sha256(path.read_bytes()).hexdigest()
for path in sorted((root / 'screenshots').glob('after-*.bmp')):
    after_hash[path.name.removeprefix('after-')] = hashlib.sha256(path.read_bytes()).hexdigest()
screen_changes = {
    key: {'before': before_hash.get(key), 'after': after_hash.get(key)}
    for key in sorted(set(before_hash) | set(after_hash))
    if before_hash.get(key) != after_hash.get(key)
}

report = {
    'probe': 'wake-and-capture',
    'server_version': read('version.json').get('server_version'),
    'wake_action': action,
    'wake_event': read('wake-event.json'),
    'wake_accepted': bool(responses) and all(item.get('accepted') is True
                                             for item in responses),
    'wake_responses': responses,
    'pointer_trace_events_before': len(before_pointer_events),
    'pointer_trace_events_after': len(pointer_events),
    'pointer_trace_events_delta': max(0, len(pointer_events) - len(before_pointer_events)),
    'button_trace_events_before': len(before_button_events),
    'button_trace_events_after': len(button_events),
    'button_trace_events_delta': max(0, len(button_events) - len(before_button_events)),
    'surface_revision_changes': revision_changes,
    'screen_hash_changes': screen_changes,
    'frame_delta': after_perf.get('frames', 0) - before_perf.get('frames', 0),
    'present_delta': after_perf.get('present_calls', 0) - before_perf.get('present_calls', 0),
    'missed_deadline_delta': after_perf.get('missed_deadlines', 0) - before_perf.get('missed_deadlines', 0),
    'interpretation': (
        'screen changed after the API wake event; inspect after screenshot and '
        'traces to determine whether Hibr woke or continued its animation'
        if screen_changes or revision_changes else
        'no surface revision or screenshot change was observed after the API '
        'wake event; the screen saver was not proven stopped'
    ),
}
(root / 'verdict.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
print(json.dumps(report, separators=(',', ':')))
PY

printf 'wake_capture_dir=%s\n' "$output_dir"
