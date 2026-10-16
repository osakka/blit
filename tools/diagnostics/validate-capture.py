#!/usr/bin/env python3
"""Validate one read-only Blit diagnostic capture directory."""

import json
import pathlib
import sys


REQUIRED = (
    "capture-metadata.json",
    "capture-complete.json",
    "version.json",
    "server.json",
    "graphics.json",
    "audio.json",
    "displays.json",
    "topology.json",
    "input-devices.json",
    "input-routes.json",
    "fonts.json",
    "settings.json",
    "diagnostics-start.json",
    "runtime-start.json",
    "trace-start.json",
    "diagnostics-end.json",
    "runtime-end.json",
    "trace-end.json",
)


def load(root: pathlib.Path, name: str):
    path = root / name
    if not path.is_file():
        raise ValueError(f"missing {name}")
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as error:
        raise ValueError(f"invalid JSON in {name}: {error}") from error


def main() -> int:
    if len(sys.argv) != 2:
        raise SystemExit("usage: validate-capture.py CAPTURE_DIRECTORY")
    root = pathlib.Path(sys.argv[1])
    try:
        metadata = load(root, "capture-metadata.json")
        complete = load(root, "capture-complete.json")
        for name in REQUIRED[2:]:
            load(root, name)
        if metadata.get("capture") != "blit-live-diagnostics-v2":
            raise ValueError("unsupported capture metadata version")
        if metadata.get("authentication") not in {"none", "bearer", "bearer-file"}:
            raise ValueError("invalid authentication mode")
        if complete.get("status") != "complete":
            raise ValueError("capture-complete.json does not report complete")
        if "Bearer " in json.dumps(metadata):
            raise ValueError("capture metadata contains an authorization header")

        displays = load(root, "displays.json")
        display_ids = []
        for item in displays.get("displays", displays.get("outputs", [])):
            value = str(item.get("id", ""))
            if value:
                display_ids.append(value if value.startswith("display-") else f"display-{value}")
        screenshot_dir = root / "screenshots"
        screenshots = sorted(screenshot_dir.glob("*.bmp")) if screenshot_dir.is_dir() else []
        if metadata.get("screenshots") == "yes":
            if not screenshots:
                raise ValueError("screenshots were requested but none were captured")
            for screenshot in screenshots:
                header = screenshot.with_name(screenshot.name + ".headers")
                if not header.is_file():
                    raise ValueError(f"missing screenshot headers for {screenshot.name}")
                text = header.read_text(encoding="utf-8", errors="replace")
                if "X-Blit-Capture-Frame:" not in text:
                    raise ValueError(f"missing capture frame header for {screenshot.name}")
        print(
            "CAPTURE_VALIDATION_PASS "
            f"displays={len(display_ids)} screenshots={len(screenshots)} "
            f"auth={metadata['authentication']} polls={complete.get('polls', 0)}"
        )
        return 0
    except ValueError as error:
        print(f"CAPTURE_VALIDATION_FAIL {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
