#!/usr/bin/env python3
"""Analyze one read-only Blit capture of a Hibr split/join desktop.

This is deliberately a verdict over API evidence, not a screenshot classifier.
It answers which ownership, geometry, producer, barrier, and presentation
boundaries are proven by the capture and lists the first missing boundary.
"""

import json
import pathlib
import sys


def load(root, name):
    path = root / name
    if not path.is_file():
        raise ValueError(f"missing {name}")
    return json.loads(path.read_text(encoding="utf-8"))


def session_files(root):
    inventory = load(root, "sessions.json")
    return inventory, {str(item["id"]): item for item in inventory.get("sessions", [])}


def surface_summary(root, surface_id):
    return load(root, f"summary-{surface_id.replace('/', '_')}.json")


def events(root, kind):
    result = []
    for name in (f"trace-{kind}-start.json", f"trace-{kind}-end.json"):
        path = root / name
        if path.is_file():
            result.extend(load(root, name).get("events", []))
    return result


def add(failures, key, detail):
    failures.append({"boundary": key, "detail": detail})


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: analyze-hibr-split.py CAPTURE_DIRECTORY")
    root = pathlib.Path(sys.argv[1])
    failures = []
    observations = {}

    try:
        topology = load(root, "topology.json")
        inventory, sessions = session_files(root)
        runtime = load(root, "runtime-end.json")
    except ValueError as error:
        report = {"analysis": "hibr-split-capture-v1", "result": "invalid",
                  "failures": [{"boundary": "capture", "detail": str(error)}]}
        (root / "hibr-split-verdict.json").write_text(
            json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print("HIBR_SPLIT_INVALID " + str(error))
        return 1

    panes = sorted(topology.get("panes", []),
                   key=lambda pane: (pane.get("bounds", {}).get("y", 0),
                                     pane.get("bounds", {}).get("x", 0)))
    layout = topology.get("layout", {})
    observations["layout"] = layout
    if layout.get("columns") != 1 or layout.get("rows") != 2:
        add(failures, "topology", f"expected 1x2, got {layout}")
    if len(panes) != 2:
        add(failures, "topology", f"expected two panes, got {len(panes)}")

    primary = [item for item in sessions.values()
               if item.get("state") in ("running", "starting") and
               item.get("hibr_desktop", {}).get("role") == "primary"]
    joins = [item for item in sessions.values()
             if item.get("state") in ("running", "starting") and
             item.get("hibr_desktop", {}).get("role") == "join"]
    observations["running_primary_ids"] = [item.get("id") for item in primary]
    observations["running_join_ids"] = [item.get("id") for item in joins]
    if len(primary) != 1:
        add(failures, "hibr_owner", f"expected one running primary, got {len(primary)}")
    if len(joins) != 1:
        add(failures, "hibr_owner", f"expected one running join, got {len(joins)}")
    if primary and joins:
        primary_name = primary[0].get("hibr_desktop", {}).get("session")
        join_name = joins[0].get("hibr_desktop", {}).get("session")
        if not primary_name or primary_name != join_name:
            add(failures, "hibr_owner",
                f"primary/join session names differ: {primary_name!r}/{join_name!r}")

    pane_by_id = {pane.get("id"): pane for pane in panes}
    if primary:
        if len(panes) == 2 and primary[0].get("pane_id") != panes[1].get("id"):
            add(failures, "placement", "primary is not in the bottom pane")
        if primary[0].get("surface_id") != pane_by_id.get(primary[0].get("pane_id"), {}).get("surface_id"):
            add(failures, "placement", "primary surface does not match its pane")
    if joins:
        if len(panes) == 2 and joins[0].get("pane_id") != panes[0].get("id"):
            add(failures, "placement", "join is not in the top pane")
        if joins[0].get("surface_id") != pane_by_id.get(joins[0].get("pane_id"), {}).get("surface_id"):
            add(failures, "placement", "join surface does not match its pane")

    output = (topology.get("outputs") or [{}])[0]
    output_bounds = output.get("bounds", {})
    regions = [pane.get("pixel_region", {}) for pane in panes]
    for index, region in enumerate(regions):
        if (region.get("x", -1) < output_bounds.get("x", 0) or
                region.get("y", -1) < output_bounds.get("y", 0) or
                region.get("x", 0) + region.get("width", 0) > output_bounds.get("x", 0) + output_bounds.get("width", 0) or
                region.get("y", 0) + region.get("height", 0) > output_bounds.get("y", 0) + output_bounds.get("height", 0)):
            add(failures, "geometry", f"pane {index} extends outside its output")
    if len(regions) == 2:
        first, second = regions
        if first.get("x") != second.get("x") or first.get("width") != second.get("width"):
            add(failures, "geometry", "stacked panes do not have equal horizontal bounds")
        if first.get("y", 0) + first.get("height", 0) != second.get("y"):
            add(failures, "geometry", "stacked pane pixel regions have a gap or overlap")
    observations["pane_pixel_regions"] = regions

    for item in primary + joins:
        surface_id = item.get("surface_id")
        if not surface_id:
            add(failures, "surface", f"session {item.get('id')} has no surface")
            continue
        try:
            summary = surface_summary(root, surface_id)
        except ValueError as error:
            add(failures, "surface", str(error))
            continue
        observations.setdefault("surfaces", {})[surface_id] = {
            "revision": summary.get("revision"),
            "width": summary.get("width"),
            "height": summary.get("height"),
            "non_empty_cells": summary.get("non_empty_cells"),
            "graphics_images": summary.get("graphics_images"),
            "frame_open": summary.get("frame_open"),
            "damage": summary.get("damage"),
        }
        if summary.get("frame_open"):
            add(failures, "atomic_frame", f"surface {surface_id} has an open frame")
        if not summary.get("non_empty_cells", 0):
            add(failures, "surface", f"surface {surface_id} is empty")

    performance = runtime.get("performance", {})
    render = runtime.get("snapshot", {}).get("render", {})
    observations["timing"] = {
        "refresh_hz": (render.get("outputs") or [{}])[0].get("refresh_hz"),
        "effective_interval_ms": render.get("effective_interval_ms"),
        "missed_deadlines": performance.get("missed_deadlines"),
        "max_render_ms": performance.get("max_render_ms"),
        "max_loop_gap_ms": performance.get("max_loop_gap_ms"),
    }
    if performance.get("missed_deadlines", 0) > 0:
        add(failures, "presentation", f"missed deadlines: {performance['missed_deadlines']}")

    barrier_events = events(root, "hibr_frame_barrier")
    barrier_events += events(root, "hibr_frame_barrier_release")
    barrier_events += events(root, "hibr_frame_barrier_timeout")
    timeout_count = sum(1 for event in barrier_events
                        if event.get("kind") == "hibr_frame_barrier_timeout")
    observations["frame_barrier"] = {
        "events": len(barrier_events),
        "timeouts": timeout_count,
    }
    if timeout_count:
        add(failures, "atomic_frame", f"Hibr barrier timeouts: {timeout_count}")

    screenshot_hashes = []
    hash_file = root / "screenshot-sha256.txt"
    if hash_file.is_file():
        screenshot_hashes = [line.split()[0] for line in hash_file.read_text().splitlines()
                             if line.split()]
    observations["screenshots"] = {
        "samples": len(screenshot_hashes),
        "unique_hashes": len(set(screenshot_hashes)),
    }
    if len(screenshot_hashes) > 1 and len(set(screenshot_hashes)) > 1:
        observations["screenshots"]["note"] = "scene changed during capture; visual stability is not asserted"

    result = "pass" if not failures else "incomplete"
    report = {
        "analysis": "hibr-split-capture-v1",
        "result": result,
        "first_missing_boundary": failures[0]["boundary"] if failures else None,
        "failures": failures,
        "observations": observations,
    }
    (root / "hibr-split-verdict.json").write_text(
        json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"HIBR_SPLIT_{result.upper()} " + json.dumps(report, separators=(",", ":")))
    return 0 if not failures else 1


if __name__ == "__main__":
    raise SystemExit(main())
