#!/usr/bin/env python3
"""Classify one temporal physical-box capture at the first missing boundary."""

import json
import pathlib
import sys


def read(root, name):
    return json.loads((root / name).read_text(encoding="utf-8"))


def perf(doc):
    return doc.get("performance", {})


def events(root, kind, edge):
    return read(root, f"trace-{kind}-{edge}.json").get("events", [])


def delta(end, start, key):
    return (perf(end).get(key, 0) or 0) - (perf(start).get(key, 0) or 0)


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: analyze-troubleshoot.py CAPTURE_DIRECTORY")
    root = pathlib.Path(sys.argv[1])
    start = read(root, "runtime-start.json")
    end = read(root, "runtime-end.json")
    p0, p1 = perf(start), perf(end)
    samples = []
    for line in (root / "runtime-samples.jsonl").read_text(encoding="utf-8").splitlines():
        try:
            item = json.loads(line)
            if "runtime" in item:
                samples.append(item["runtime"])
        except json.JSONDecodeError:
            continue

    def count(kind, device=None):
        start_seq = max((x.get("sequence", 0) for x in events(root, kind, "start")), default=0)
        values = [e for e in events(root, kind, "end")
                  if e.get("sequence", 0) > start_seq]
        if device is not None:
            values = [e for e in values if device in e.get("detail", "")]
        return len(values)

    physical_motion = count("pointer_motion", "device=evdev-pointer")
    physical_buttons = count("pointer_button", "device=evdev-pointer")
    render_events = count("render_path")
    surface_events = count("surface_update")
    evdev = delta(end, start, "evdev_mouse_events")
    evdev_motion = delta(end, start, "evdev_mouse_motion_events")
    evdev_buttons = delta(end, start, "evdev_mouse_button_events")
    present = delta(end, start, "present_calls")
    commits = delta(end, start, "surface_commits")
    elapsed_ms = (p1.get("uptime_ms", 0) or 0) - (p0.get("uptime_ms", 0) or 0)
    elapsed_s = elapsed_ms / 1000.0 if elapsed_ms > 0 else 0.0
    present_hz = present / elapsed_s if elapsed_s else 0.0
    outputs = end.get("snapshot", {}).get("render", {}).get("outputs", [])
    refresh = outputs[0].get("refresh_hz", 0) if outputs else 0
    warnings = []

    if evdev <= 0 or physical_motion <= 0:
        verdict = "physical-input-not-observed"
        detail = "No physical evdev motion crossed the API during the timed window."
    elif render_events <= 0:
        verdict = "input-routing-or-render-boundary"
        detail = "Physical motion was accepted, but no render-path event followed it."
    elif surface_events > 0 and render_events > 0 and present <= 0:
        verdict = "presentation-stalled"
        detail = "Surface and render boundaries advanced, but presentation did not."
    elif refresh and present_hz < refresh * 0.5 and commits > 0:
        verdict = "presentation-cadence"
        detail = "Surface commits occurred, but damage-driven presentation was below half the display refresh target."
    else:
        verdict = "no-first-boundary-failure"
        detail = "All observed boundaries advanced; inspect the correlated screenshot and trace details."

    if delta(end, start, "missed_deadlines") > 0:
        warnings.append("missed-render-deadline")
    if any((s.get("performance", {}).get("last_loop_gap_ms", 0) or 0) > 1000
           for s in samples):
        warnings.append("loop-gap-over-1000ms")
    if physical_buttons == 0 and evdev_buttons > 0:
        warnings.append("button-counter-without-button-trace")

    report = {
        "analysis": "troubleshoot-experience-v1",
        "capture_directory": str(root),
        "verdict": verdict,
        "detail": detail,
        "warnings": warnings,
        "elapsed_ms": elapsed_ms,
        "observations": {
            "refresh_hz": refresh,
            "present_hz_damage_driven": round(present_hz, 3),
            "present_calls": present,
            "surface_commits": commits,
            "physical_evdev_events": evdev,
            "physical_evdev_motion_events": evdev_motion,
            "physical_evdev_button_events": evdev_buttons,
            "physical_pointer_motion_trace_events": physical_motion,
            "physical_pointer_button_trace_events": physical_buttons,
            "render_path_trace_events": render_events,
            "surface_update_trace_events": surface_events,
            "missed_deadlines_delta": delta(end, start, "missed_deadlines"),
            "max_loop_gap_ms": p1.get("max_loop_gap_ms"),
        },
        "verdict_semantics": "first-missing-boundary-v1",
    }
    (root / "verdict.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print("TROUBLESHOOT_VERDICT", json.dumps(report, separators=(",", ":")))


if __name__ == "__main__":
    main()
