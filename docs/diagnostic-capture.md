# Live diagnostic capture

## Reproducing a physical-pointer report

Use the pointer artifact capture in `physical` mode while moving and clicking
the real pointer:

```sh
tools/diagnostics/pointer-artifact-capture.sh \
  http://192.168.10.54:8080 diagnostics/experience-$(date -u +%Y%m%dT%H%M%SZ) no physical
```

Use `api` mode as the independent control path:

```sh
tools/diagnostics/pointer-artifact-capture.sh \
  http://192.168.10.54:8080 diagnostics/experience-api-$(date -u +%Y%m%dT%H%M%SZ) yes api
```

The capture separates the failure classes: `runtime-physical.json` reports
whether evdev mouse counters increased, pointer traces report what Blit
accepted, render traces report pointer-cache versus full-redraw composition,
and the BMP hashes provide visual evidence. Zero physical evdev events means
the input/device path is the blocker; accepted events with bad BMPs means the
Blit presentation path; correct BMPs with incorrect terminal content means
the terminal surface. Keep screenshots disabled for timing/input captures:
the SDL screenshot readback is a heavyweight operation and can perturb the
single-threaded render loop. It does not start or modify Hibr.

The pointer capture tolerates a host with no active Hibr session: the session
JSON is recorded as `available:false` and the input/render capture continues.
This is intentional, because a missing session must not hide a compositor or
device-inventory failure. A live Hibr visual reproduction still requires a
session to be present when the physical interaction occurs.

## Deterministic wallpaper/pointer regression

The live Hibr desktop is a large opaque terminal surface, so the small atomic
pointer test is not sufficient by itself. Run the full-surface stress gate
after a renderer change:

```sh
ruby tools/tests/pointer_wallpaper_stress_gate.rb
```

It sends the surface in bounded deferred chunks followed by one commit, then
interleaves pointer motion with further commits. Six samples outside the
changed cells must remain non-black, the trace must show the pointer-plus-
damage incremental-damage boundary, and a pointer-only update must use the cached
scene. This reproduces the compositor ordering without changing Hibr or
requiring a physical device.

The gate is a renderer regression test, not physical-input acceptance. The
physical acceptance boundary remains `verify-live-physical.sh`, run while the
real pointer is being moved.

The real-host capture harness records Blit's API state without depending on a
local Hibr shell or a physical keyboard. It collects the hardware/topology
inventory, runtime counters, bounded event trace, and synchronized framebuffer
screenshots.

For a low-cost surface check, use the compact summary endpoint instead of the
full cell snapshot:

```text
/v1/surfaces/surface-1/summary
```

It returns revision, dimensions, non-empty-cell count, cursor state, graphics
count, and the current damage rectangle. The full `/cells` endpoint remains
the authoritative repair snapshot, but can be megabytes for a desktop-sized
surface and must not be used in timing loops.
`capture-live.sh` records `summary-surface-1.json` at the start and end of a
capture. Set `BLIT_SURFACE_IDS=surface-1,surface-2` when collecting multiple
known surfaces; the capture remains bounded and does not download their full
cell grids.
The capture harness requests eight newest trace events by default; set
`BLIT_TRACE_LIMIT=1..128` to choose another bounded window.
During the timed interval it polls only runtime counters and the compact
surface summary. The filtered trace views are taken at the start and end so
the capture itself does not repeatedly serialize diagnostic history on the
single-threaded gateway.

Because the current API is resource-oriented and does not expose a broad
session-list response, the harness also probes the bounded session ID range
`1..16` and writes `session-N.json` only for sessions that exist. Set
`BLIT_SESSION_IDS=3,10` when a run should inspect only known sessions. This
never creates, focuses, resizes, or deletes a session.

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

Trace responses are also bounded at the HTTP boundary: the default is the 32
newest matching events, and callers may request `limit=1..128`. This prevents a
diagnostic client or a slow network from turning observation into a large
single-threaded response. For example:

```text
/v1/diagnostics/trace?kind=render_path&limit=16
```

Use the smallest limit that answers the question. A missing or invalid limit
uses the safe default; the oldest matching events are discarded first.

`surface_update` records the `immediate` and `commit` boundaries, not every
deferred cell chunk. This keeps the bounded trace useful under high-volume
terminal output while preserving the atomic-frame evidence.

## Real-host troubleshooting protocol

Use one capture as the evidence packet for each visible failure. Do not infer
frame rate from how fast the screen appears to change: compare
`runtime-start.json` and `runtime-end.json`, especially `frame_rate_hz`,
`frame_rate_frames`, `missed_deadlines`, `max_loop_gap_ms`, and the evdev input
counters. Classify the failure at the first missing boundary:

1. no physical event;
2. event not routed;
3. PTY or surface did not change;
4. surface changed but no atomic render path followed;
5. render or presentation missed its budget.

Run a timing-only capture while reproducing the problem, then take a separate
screenshot capture. Keeping screenshots out of the timing interval matters:
the framebuffer download is deliberately isolated because it can otherwise
disturb the single-threaded API/render loop.

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
The verifier also compares the start/end runtime counters and requires positive
`evdev_mouse_events` and `evdev_mouse_motion_events` deltas. A trace-only event
or a missing runtime response is not accepted as physical-input evidence.
It reports both the maximum loop gap observed during the capture and the
runtime's lifetime maximum; an old screenshot or diagnostic stall therefore
cannot be mistaken for a stall caused by the current physical-input test.
For machines with multiple pointer nodes, inspect `performance.evdev_mouse_devices`
in `/v1/diagnostics/runtime`: each entry reports the opened path, file
descriptor, and event/motion/button/wheel counters. This distinguishes an
unmoved node from a wrong-node or duplicate-device diagnosis without guessing
from the aggregate counter.

The Hibr adapter batches up to 256 cell mutations per request, while retaining
deferred updates and one final `commit` per Hibr flush. This bounds transport
overhead without exposing a partial frame. The batch is still limited by the
adapter's fixed request buffer and the server's bounded HTTP body policy.

The pointer/damage regression gate verifies the SDL atomic boundary directly:

```sh
ruby tools/tests/pointer_damage_atomic_gate.rb
```

It queues pointer input and an external surface commit in the same event-loop
turn, requires a `pointer_dirty=1` incremental-damage trace, and samples pixels outside
the changed cell. A pass proves that pointer motion cannot expose cleared SDL
background outside the damage rectangle; the separate external-pointer gate
continues to prove that pointer-only motion uses the cached scene.

## Pointer-artifact transaction

When pointer movement appears to make an external Hibr desktop blink, trail, or
repaint inconsistently, capture the complete interaction boundary from a
reachable client:

```sh
tools/diagnostics/pointer-artifact-capture.sh \
  http://192.168.10.54:8080 diagnostics/pointer-live no
```

The transaction records the deployed version and topology, the active session,
runtime timing before and after each motion, and filtered
pointer/render/surface traces. It is screenshot-free by default so the
diagnostic transaction cannot be confused with a renderer stall caused by a
large framebuffer transfer. Pass `yes` as the third argument to capture a
screenshot after each injected position; use that only after the timing
transaction has completed, or interpret the resulting transfer separately.
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

When diagnosing the Hibr adapter itself, enable its opt-in transport trace for
one disposable probe session:

```sh
BLIT_ADAPTER_TRACE=1
```

The trace is emitted by the Blit-owned adapter and reports WebSocket dial and
handshake failures, image requests, pixel-frame transport failures, and
successful frame submission. It does not modify Hibr. The authoritative result
is still the Blit runtime: `pixel_frames_accepted` must increase and
`pixel_frame_errors` must remain unchanged. Do not enable this continuously on
a busy desktop; it is a probe aid, not a replacement for the runtime counters.
