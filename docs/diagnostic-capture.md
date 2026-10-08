# Live diagnostic capture

The real-host capture harness records Blit's API state without depending on a
local Hibr shell or a physical keyboard. It collects the hardware/topology
inventory, runtime counters, bounded event trace, and synchronized framebuffer
screenshots.

Run it from a machine that can reach Blit:

```sh
tools/diagnostics/capture-live.sh http://192.168.10.54:8080 20
```

For a timing-only run that does not issue the large framebuffer screenshot
request, pass `no` as the fourth argument:

```sh
tools/diagnostics/capture-live.sh http://192.168.10.54:8080 20 diagnostics/live-timing no
```

Use the timing-only form when measuring frame cadence, loop gaps, or input
latency. Use the default screenshot form when visual evidence is required;
the screenshot endpoint is intentionally kept separate because its transfer
can block a single-threaded server for hundreds of milliseconds. It also
requests a fresh scene (`initialized=0`) so the returned image is current;
therefore do not use screenshot requests inside a pointer-latency or redraw
cadence measurement. Take screenshots after the interaction window instead.

The output directory contains:

- `trace-*.json`: input events, surface presentation boundaries, and renderer
  path decisions;
- `runtime-*.json`: frame timing, input counters, stream health, and session
  resources;
- `session-*.json`: the selected terminal session's surface revision and
  graphics objects, including whether an image is being replaced, hidden, or
  repeatedly recreated;
- `screenshots/*.bmp` and `screenshot-sha256.txt`: framebuffer evidence;
- the start/end hardware and topology snapshots.

Interpretation is causal: a physical click must appear as `pointer_button`,
the associated Hibr surface activity must follow a `commit`, and the renderer
must report the path used for the resulting presentation. Missing events or a
hash change without a corresponding commit are distinct, actionable failures.
A `render_path` event with `graphics_changed=1` must correlate with a change
in the matching `session-*.json` graphics list; otherwise the compositor's
graphics-scene boundary is wrong. A `pointer-cache` path should not require
a full scene rebuild.

The general trace is intentionally bounded, and a busy Hibr surface can fill
it with surface updates. The harness therefore also requests filtered views:

```text
/v1/diagnostics/trace?kind=pointer_button
/v1/diagnostics/trace?kind=pointer_motion
/v1/diagnostics/trace?kind=keyboard_key
/v1/diagnostics/trace?kind=surface_update
/v1/diagnostics/trace?kind=render_path
```

Filtering changes only the diagnostic response; it does not alter input,
routing, Hibr, or presentation.

`surface_update` records the `immediate` and `commit` boundaries, not every
deferred cell chunk. This keeps the bounded trace useful under high-volume
terminal output while preserving the atomic-frame evidence.

For deterministic pointer diagnostics, inject events through the same Blit
path used by evdev:

```sh
curl -X POST http://192.168.10.54:8080/v1/input/inject \
  -H 'Content-Type: application/json' \
  -d '{"kind":"pointer_motion","x":960,"y":1080}'
curl -X POST http://192.168.10.54:8080/v1/input/inject \
  -H 'Content-Type: application/json' \
  -d '{"kind":"pointer_button","button":0,"pressed":true}'
```

These are synthetic API events and are reported as `api-pointer`; they do not
claim that the physical evdev device generated the event.

For a repeatable input-to-render check, run the probe. It records the before
and after runtime counters plus filtered traces, and fails if motion, both
button edges, dispatch accounting, pointer position, or a render path event is
missing:

```sh
tools/diagnostics/verify-live-input.sh http://192.168.10.54:8080
```

For physical-device acceptance, run the screenshot-free gate while moving the
real mouse. It rejects API-injected events, requires the pointer device to be
present in the inventory, and fails distinctly when no physical motion was
observed:

```sh
tools/diagnostics/verify-live-physical.sh \
  http://192.168.10.54:8080 10 diagnostics/physical-test
```

Exit status `2` means the capture was valid but no physical motion occurred;
it is not a renderer pass or failure. Exit status `1` means a device,
render-path, deadline, or empty-graphics redraw invariant failed.

The probe is deliberately synthetic. Physical-device diagnosis uses the same
capture directory and must additionally show `device_id`/evdev events from
`/v1/diagnostics/trace` and the corresponding input-device state in
`/v1/input/devices`. A screenshot request is a large transfer and therefore
can perturb a single-threaded server; compare its timestamps with
`max_loop_gap_ms` before treating a captured stall as a rendering stall.

The Hibr adapter batches up to 256 cell mutations per request, while retaining
deferred updates and one final `commit` per Hibr flush. This bounds transport
overhead without exposing a partial frame. The batch is still limited by the
adapter's fixed request buffer and the server's bounded HTTP body policy.

## Pointer-artifact transaction

When pointer movement appears to make an external Hibr desktop blink, trail, or
repaint inconsistently, capture the complete interaction boundary from a
reachable client:

```sh
tools/diagnostics/pointer-artifact-capture.sh \
  http://192.168.10.54:8080 diagnostics/pointer-live
```

The transaction records the deployed version and topology, the active session,
runtime timing before and after each motion, filtered pointer/render/surface
traces, and a screenshot plus SHA-256 after every injected pointer position.
The injected events use the same API dispatch path as physical input and are
labelled `api-pointer`; they are a controlled comparator, not evidence about
the physical evdev device. Compare `render_path` with the screenshots: a
pointer-only frame should use `pointer-cache`. A full redraw with
`external_changed=1 graphics_changed=0` identifies an external-cell damage
path, while `graphics_changed=1` identifies the intentionally expensive image
or graphics-scene rebuild path.

The adapter also advertises Kitty-compatible pixel placement to Hibr and sends
image frames through Blit's bounded WebSocket pixel protocol. Static under-text
images use `imgkeep` and are not retransmitted; clearing one hides the cached
image before the next atomic scene presentation.
