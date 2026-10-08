#!/usr/bin/env bash
set -euo pipefail

base_url=${1:?usage: verify-live-physical.sh http://host:8080 [seconds] [output-dir]}
duration=${2:-10}
output_dir=${3:-"diagnostics/physical-$(date -u +%Y%m%dT%H%M%SZ)"}

tools_dir=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
"$tools_dir/capture-live.sh" "$base_url" "$duration" "$output_dir" no

python3 - "$output_dir" <<'PY'
import json
import pathlib
import sys

root = pathlib.Path(sys.argv[1])

def read(name):
    return json.loads((root / name).read_text())

def events(name):
    return read(name).get("events", [])

start_trace = read("trace-pointer_motion-start.json")
start_sequence = max((event.get("sequence", 0)
                      for event in start_trace.get("events", [])), default=0)
motion = [event for event in events("trace-pointer_motion-end.json")
          if event.get("sequence", 0) > start_sequence]
buttons = [event for event in events("trace-pointer_button-end.json")
           if event.get("sequence", 0) > start_sequence]
render = [event for event in events("trace-render_path-end.json")
          if event.get("sequence", 0) > start_sequence]

physical_motion = [event for event in motion
                   if "device=evdev-pointer" in event.get("detail", "")]
physical_buttons = [event for event in buttons
                    if "device=evdev-pointer" in event.get("detail", "")]
full_empty = [event for event in render
              if "graphics_changed=1" in event.get("detail", "") and
                 "graphics_images=0" in event.get("detail", "")]
runtime = read("runtime-end.json").get("performance", {})
devices = read("input-devices.json").get("devices", [])
pointer_devices = [device for device in devices
                   if "pointer" in device.get("kinds", [])]

print("physical_motion_events=%d physical_button_events=%d render_events=%d" %
      (len(physical_motion), len(physical_buttons), len(render)))
print("missed_deadlines=%s last_render_ms=%s max_loop_gap_ms=%s" %
      (runtime.get("missed_deadlines"), runtime.get("last_render_ms"),
       runtime.get("max_loop_gap_ms")))

if not pointer_devices:
    print("FAIL physical-pointer-device-not-inventory")
    raise SystemExit(1)
if not physical_motion:
    print("PHYSICAL_INPUT_NOT_OBSERVED move the real mouse during the capture")
    raise SystemExit(2)
if not render:
    print("FAIL no-render-path-after-physical-input")
    raise SystemExit(1)
if runtime.get("missed_deadlines", 0) != 0:
    print("FAIL missed-refresh-deadline")
    raise SystemExit(1)
if full_empty:
    print("FAIL empty-graphics-full-redraw count=%d" % len(full_empty))
    raise SystemExit(1)
print("LIVE_PHYSICAL_INPUT_PASS")
PY
