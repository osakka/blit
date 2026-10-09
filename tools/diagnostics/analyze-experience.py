#!/usr/bin/env python3
"""Analyze one experience-capture directory without treating cumulative counters as a rate."""

import json
import pathlib
import sys


def load(root: pathlib.Path, name: str):
    with (root / name).open(encoding="utf-8") as stream:
        return json.load(stream)


def performance(doc):
    return doc.get("performance", {})


def snapshot(doc):
    return doc.get("snapshot", {})


def resources(doc):
    return snapshot(doc).get("resources", {})


def delta(end, start, key):
    return performance(end).get(key, 0) - performance(start).get(key, 0)


def trace_sequence(root: pathlib.Path, name: str):
    path = root / name
    if not path.exists():
        return None
    events = load(root, name).get("events", [])
    return events[-1].get("sequence") if events else 0


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: analyze-experience.py CAPTURE_DIRECTORY")
    root = pathlib.Path(sys.argv[1])
    start = load(root, "runtime-start.json")
    end = load(root, "runtime-end.json")
    start_uptime = performance(start).get("uptime_ms")
    end_uptime = performance(end).get("uptime_ms")
    elapsed_ms = max(0, (end_uptime or 0) - (start_uptime or 0))
    elapsed_s = elapsed_ms / 1000.0

    present_delta = delta(end, start, "present_calls")
    frame_delta = delta(end, start, "frames")
    missed_delta = delta(end, start, "missed_deadlines")
    input_delta = delta(end, start, "input_events")
    evdev_delta = delta(end, start, "evdev_mouse_events")
    motion_delta = delta(end, start, "evdev_mouse_motion_events")
    button_delta = delta(end, start, "evdev_mouse_button_events")
    wheel_delta = delta(end, start, "evdev_mouse_wheel_events")
    terminal_bytes_delta = delta(end, start, "terminal_bytes")
    graphics_upload_delta = delta(end, start, "graphics_texture_uploads")

    rate = (present_delta / elapsed_s) if elapsed_s else 0.0
    output_rates = []
    for output in snapshot(end).get("render", {}).get("outputs", []):
        output_rates.append({
            "display_id": output.get("display_id"),
            "refresh_hz": output.get("refresh_hz"),
            "missed_deadlines": output.get("missed_deadlines"),
            "last_present_ms": output.get("last_present_ms"),
            "max_present_ms": output.get("max_present_ms"),
        })

    warnings = []
    if not snapshot(end).get("health", {}).get("ready", False):
        warnings.append("server_not_ready")
    if elapsed_s <= 0:
        warnings.append("non_positive_capture_interval")
    if missed_delta > 0:
        warnings.append("missed_render_deadlines")
    if present_delta == 0:
        warnings.append("no_present_counter_progress")
    # Blit is damage-driven: an unchanged scene is intentionally not
    # re-presented, so present_calls is not a vblank/frame-rate counter. Only
    # classify a low present rate as suspicious while the capture observed
    # active terminal, graphics, or input work.
    active_work = (terminal_bytes_delta > 0 or graphics_upload_delta > 0 or
                   input_delta > 0 or evdev_delta > 0 or frame_delta > 0)
    if (active_work and output_rates and
            rate < max(1.0, float(output_rates[0].get("refresh_hz") or 0) * 0.5)):
        warnings.append("active_present_rate_below_half_refresh")
    if evdev_delta == 0:
        warnings.append("no_physical_evdev_mouse_events")

    report = {
        "analysis": "experience-capture-delta-v1",
        "capture_directory": str(root),
        "elapsed_ms": elapsed_ms,
        "deltas": {
            "present_calls": present_delta,
            "frames": frame_delta,
            "estimated_present_hz": round(rate, 3),
            "missed_deadlines": missed_delta,
            "input_events": input_delta,
            "evdev_mouse_events": evdev_delta,
            "evdev_mouse_motion_events": motion_delta,
            "evdev_mouse_button_events": button_delta,
            "evdev_mouse_wheel_events": wheel_delta,
            "terminal_bytes": terminal_bytes_delta,
            "graphics_texture_uploads": graphics_upload_delta,
        },
        "resources": resources(end),
        "outputs": output_rates,
        "trace_end_sequences": {
            kind: trace_sequence(root, f"trace-{kind}-end.json")
            for kind in ("pointer_motion", "pointer_button", "keyboard_key", "surface_update", "render_path")
        },
        "warnings": warnings,
        "present_rate_semantics": "damage-driven-present-calls-not-vblank-rate",
        "active_work_observed": active_work,
        "result": "observation_complete",
    }
    (root / "analysis.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print("EXPERIENCE_ANALYSIS", json.dumps(report, separators=(",", ":")))


if __name__ == "__main__":
    main()
