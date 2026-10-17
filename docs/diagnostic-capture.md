# Live diagnostic capture

Direct KMS exposes clock evidence at
`GET /v1/diagnostics/trace?kind=kms_vblank&limit=16`. Events report the display,
completed flips, DRM sequence/timestamp, accumulated vblank count/time, observed
refresh, and nominal mode refresh. Sequence/time deltas measure the DRM clock
even when Blit submits occasional frames; completions count submitted flips.
Events occur on first completion and every 60 completions. See ADR 0120.

## Physical symptom capture (canonical)

For a report involving mouse movement, blinking, trails, missing clicks, or
intermittent redraws, run this from a reachable Bash host:

```sh
# This must be the API bearer token, not the separate package-update token.
export BLIT_AUTH_TOKEN_FILE=/secure/path/to/api-token
tools/diagnostics/troubleshoot-experience.sh \
  http://192.168.10.54:8080 15 \
  diagnostics/troubleshoot-$(date -u +%Y%m%dT%H%M%SZ)
```

Use `BLIT_AUTH_TOKEN` instead when the token is already held by a secure
process environment. Never put a token in the URL, command arguments, logs,
or the capture directory. The shared diagnostic transport rejects setting
both token forms and records only the authentication mode in metadata.

While it prints `recording`, reproduce the symptom with the real mouse and
keyboard. The command writes `verdict.json`, `verdict.txt`, raw 250 ms runtime
samples, filtered traces, and one cached screenshot per display. Its verdict is
ordered at the first missing boundary: physical input, routing/render, surface
production, presentation stall, or presentation cadence. Do not interpret a
zero physical-input verdict as a renderer result; it means the reproduction
did not cross evdev during that window. Do not use the screenshots to infer
frame rate.

## One-command experience capture

Use this as the canonical evidence transaction for a report from the real
machine:

```sh
tools/diagnostics/experience-capture.sh \
  http://192.168.10.54:8080 10 diagnostics/experience-$(date -u +%Y%m%dT%H%M%SZ) yes
```

It first records ten seconds of runtime counters, surface summaries, input
inventory, and filtered traces with screenshots disabled. It then captures one
current screenshot for every discovered display. This ordering is important:
the screenshot transfer cannot be mistaken for a rendering stall. The output
directory is the complete handoff artifact; preserve it unchanged. The command
also writes `analysis.json` and `analysis.txt`. That report calculates present
rate from the `present_calls` counter delta divided by the runtime-uptime delta;
it does not use the server's cumulative maximum gap or rolling rate as a
short-capture FPS measurement. `present_calls` is damage-driven: an unchanged
scene is intentionally not re-presented. The analyzer therefore labels this
as `damage-driven-present-calls-not-vblank-rate` and only warns about a low
present rate when terminal or graphics scene work was observed. Input activity
is reported independently because a short pointer burst is not a sustained
frame-rate workload. Warnings are observations, not pass/fail claims.

The capture also records `graphics.json`, `audio.json`, `fonts.json`,
`settings.json`, `sessions.json`, `capture-metadata.json`, and
`capture-complete.json`. `sessions.json` is the authoritative live session
inventory from `GET /v1/terminal-sessions`; captures no longer probe a guessed
numeric session range. Each
`screenshots/*.bmp` has a matching `.headers` file retaining the API capture
frame and surface-revision identity. These files let a screenshot be compared
with the exact runtime and surface state that produced it. The command writes
`validation.txt` only after the required JSON and screenshot/header invariants
pass; a missing or malformed artifact makes the capture fail.

Interpret the first missing boundary in this order:

1. `physical_evdev_events` did not increase: device/permissions/seat input;
2. input increased but no `pointer_motion`/`pointer_button` trace: routing;
3. input and trace exist but no surface revision/commit: Hibr/PTY surface;
4. surface changed but no `render_path`: compositor scheduling;
5. render path exists with missed deadlines or a large loop gap: presentation;
6. all counters are healthy but the screenshot is wrong: framebuffer composition.

For a pure timing capture use `no` as the final argument. For a controlled
comparison, run the same command with API-injected pointer events and compare
it with the physical capture; this separates evdev problems from renderer
problems without changing Hibr.

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

The runtime performance object also reports `surface_commits`,
`surface_commit_hz`, `surface_commit_age_ms`, and `frame_commits`. These are
the producer-side boundaries for a terminal capture: a low
`surface_commit_hz` with a healthy output `frame_rate_hz` means the producer
committed slowly; a high commit rate with a low presentation rate points at
Blit scheduling or presentation. A zero cadence after the bounded age means
the producer is idle or stale, not that the display stopped refreshing.
Terminal byte volume alone is not a frame-rate measurement.

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

The screenshot is an independent correctness gate, not decoration. If it is
incomplete, duplicated, stale, flat where a known wallpaper should be, or
otherwise inconsistent with the expected scene, the visual result is failed
even when API input was accepted, sessions are alive, render timing is within
budget, or local deterministic gates pass. Do not describe that result as
“stable” or “working”; identify the first missing boundary and preserve the
capture.

The real-host capture harness records Blit's API state without depending on a
local Hibr shell or a physical keyboard. It collects the hardware/topology
inventory, runtime counters, bounded event trace, and synchronized framebuffer
screenshots.

For the standard stacked Hibr experiment—one authenticated primary in the
bottom pane and one `--join` in the top pane—use the read-only split capture:

```sh
tools/diagnostics/hibr-split-capture.sh \
  http://192.168.10.54:8080 10 \
  diagnostics/hibr-split-$(date -u +%Y%m%dT%H%M%SZ)
```

It writes `hibr-split-verdict.json` and `.txt`. The verdict checks ownership,
pane tiling, primary/join surface identity, closed frame batches, graphics or
cell production, display refresh timing, and Hibr frame-barrier timeouts. It
does not mutate topology, sessions, input routes, Hibr settings, or login
state. A failed verdict names the first missing boundary; it does not infer a
cause from the screenshot alone.

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

The harness records the authoritative `sessions.json` inventory and writes
`session-N.json` for every live session. Set `BLIT_SESSION_IDS=3,10` when a
run should inspect only known sessions after the inventory has been captured.
This never creates, focuses, resizes, or deletes a session.

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
can block a single-threaded server for hundreds of milliseconds. Diagnostic
captures use `?cached=1` to observe the already-composed scene without
invalidating a live Hibr frame. Callers that need a deliberate full rebuild
may omit the query parameter; do not use that fresh-capture form inside a
pointer-latency or redraw-cadence measurement.

The output directory contains:

- `capture-metadata.json` and `capture-complete.json`: the bounded capture
  contract, authentication mode, requested duration/trace limit, and whether
  the transaction completed;
- `validation.txt`: the mechanical completeness result for the bundle;

- `trace-*.json`: input events, surface presentation boundaries, and renderer
  path decisions;
- `runtime-*.json`: frame timing, input counters, stream health, and session
  resources;
- `session-*.json`: the selected terminal session's surface revision and
  graphics objects, including whether an image is being replaced, hidden, or
  repeatedly recreated;
- `screenshots/*.bmp` and `screenshot-sha256.txt`: authoritative lossless
  framebuffer evidence. When `ffmpeg` is installed, matching
  `screenshots/*.png` and `screenshot-png-sha256.txt` are also emitted for
  direct inspection. Each BMP has a `.headers` companion with its capture
  frame and surface-revision identity;
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
/v1/diagnostics/trace?kind=pointer_wheel
/v1/diagnostics/trace?kind=pointer_motion
/v1/diagnostics/trace?kind=keyboard_key
/v1/diagnostics/trace?kind=surface_update
/v1/diagnostics/trace?kind=render_path
/v1/diagnostics/trace?kind=graphics_payload
```

Filtering changes only the diagnostic response; it does not alter input,
routing, Hibr, or presentation.

`analysis.json` is the canonical short-window comparison. Its `deltas` object
contains the measured present cadence, missed-deadline delta, input deltas, and
physical evdev deltas. `outputs` retains the display refresh target and present
latencies, while `trace_end_sequences` shows whether each causal boundary
advanced. A report with `no_physical_evdev_mouse_events` is not evidence that
the pointer is broken; it means no physical movement occurred during that
capture. Conversely, `present_rate_below_half_refresh` is a measured cadence
warning even when the last individual present latency looks healthy.

Trace responses are also bounded at the HTTP boundary: the default is the 32
newest matching events, and callers may request `limit=1..128`. This prevents a
diagnostic client or a slow network from turning observation into a large
single-threaded response. For example:

```text
/v1/diagnostics/trace?kind=render_path&limit=16
```

Use the smallest limit that answers the question. A missing or invalid limit
uses the safe default; the oldest matching events are discarded first.

`surface_update` records the `api-cells` transport, the owning surface mode
(`terminal`, `external`, or `mirror`), and the `immediate`/`commit` boundaries,
not every deferred cell chunk. PTY-driven terminal updates additionally record
the read byte count, resulting surface revision, damage rectangle, cursor
state, frame-batch state, and synchronization state. This keeps the bounded
trace useful under high-volume terminal output while preserving the
atomic-frame evidence.

On the direct-KMS backend, `render_path` records the output submitted and
deferred masks, KMS present time, pointer state, and each of the first four
submitted views' surface revision, damage rectangle, cursor state, and
synchronization state. Correlate a PTY `surface_update` with the following
KMS `render_path` record before attributing a visual defect to the compositor:

```text
surface_update: session=1 surface=surface-1 bytes=... revision=... damage=...
render_path: backend=kms path=direct-kms submitted_mask=... view0=surface-1/rev-.../damage-...
```

`graphics_payload` is one entry per Kitty or sixel payload that reached the
terminal parser, written after the decode so it records both what arrived and
what became of it:

```text
protocol=sixel session=session-3 bytes=1942 header=DCS0;1;0q
  images=0->1 rejected=0 reason=-
protocol=kitty session=session-3 bytes=42 header=Ga=T,f=24,s=2,v=2,t=f,q=2
  images=0->0 rejected=1 reason=unsupported-key
```

This is the only way to tell a producer that sent no image from an image Blit
decoded and discarded: both leave a session with zero images, no errors and a
healthy document (issue #19). `header` is the leading control bytes, captured
before the decoder runs, because the Kitty decoder writes into the buffer it
parses.

These entries have their own ring rather than sharing the one that holds input
and render evidence. Graphics payloads arrive at whatever rate the application
sends them -- a video player through Kitty is as busy as a terminal redrawing
-- and the point of that ring is that a busy producer cannot evict the causal
evidence; sharing it would have traded one blind spot for another. The
counters `graphics_payloads`, `graphics_payloads_kitty`,
`graphics_payloads_sixel` and `graphics_payloads_rejected` in the performance
document survive eviction of any ring, so a zero there is the strongest form
of "nothing arrived".

An unfiltered trace request still returns the surface ring, as it always has,
so `graphics_payload` has to be named to be read.

## Settings

Blit keeps its own settings and the API writes them (ADR 0067). They live in
`/var/lib/blit/settings.json`, which the unit owns through
`StateDirectory=blit`, and the supported way to read, back up, restore or
reset them is the resource rather than the file:

```text
GET    /v1/settings     the whole document, with a revision
PUT    /v1/settings     If-Match, replaces it
DELETE /v1/settings     reset to defaults
```

`PUT` replaces rather than merges, because a merge cannot express "this and
nothing else", which is what restoring a backup means. Each entry is validated
by the same code a `PATCH` on that device goes through, so a stored setting
cannot be accepted where a live one would be refused; `applied`, `refused` and
`refusal` report what happened. A refused entry is **kept**, not deleted: a
property that contradicts the kinds the kernel reports today is far likelier
to be a re-probe than a controller's mistake, so it lies dormant and applies
again when the device reads the way it did.

Precedence runs in one direction: a stored value wins, an environment variable
is the boot default **only where the document is silent**, and an accepted API
write stores. The trap is worth knowing before you hit it — once a setting has
been written through the API, editing `blit.env` for that setting changes
nothing. `DELETE /v1/settings` is the way back.

Two kinds of id are deliberately absent from the document. An
`input-node-<basename>` id says in itself that it is not stable, so restoring
against it could apply one device's settings to whatever took its place; and a
device's `route` names a `session-N` target, which does not survive a restart
either. Both are reported in the device item and both are honest gaps rather
than oversights.

The document has three sections: `input_devices`, `audio_devices` and
`displays`. A display is keyed by its **name**, not by `display-<n>`, for the
same stability reason — the name is the connector (`DP-1` on the live host)
while `display-<n>` is enumeration order. The `displays` section is also the
one whose subjects do not exist yet when the document loads, so it is applied
after the outputs are initialised and before the first frame.

What is in each section is the setting Blit owns and nothing else:

```text
displays   rotation only. Power is not persisted -- SDL's initialising
           modeset turns the panel on anyway, so a stored `off` would be a
           dark boot whose only remedy is an API nobody can see. Brightness
           is not persisted either: systemd-backlight already restores it.
audio      the per-PCM `default` flag, not mixer values. A card keeps its own
           volume, and storing it would make Blit fight alsactl for a setting
           that already survives a reboot.
```

A setting returned to its default **removes** its entry rather than writing
the default out, so the document stays a record of what somebody chose.

### When input disappears

`input_device_missing` fires when a count of open keyboards, pointers or
gamepads is **below the most this process has had** — not only when it reaches
zero. The zero test is what the warning used to do, and on 2026-10-11 it
stayed silent while a USB hub took the live host's keyboard and mouse away:
a microphone's remote-control interface was still open and still counted as
one of each, so `warnings` was empty while the operator had no usable input at
all. `resources` carries `keyboards_peak`, `pointers_peak` and `gamepads_peak`
so the warning can be acted on rather than merely noticed.

The peak is per-process on purpose. Unplugging a keyboard you no longer want
leaves the warning on until a restart, and a restart is the deliberate way to
say that the smaller number is the new normal.

Two things a hotplug burst used to break, both fixed in 0.1.460 and both
gated by `tools/tests/input_hotplug_cycle_gate.rb`:

- A rescan is the only thing that retires an absent device, and inotify arms
  it. A hub dropping thirteen nodes at once coalesces into one flag, so the
  single scan could run while some of those nodes still existed and mark them
  seen — after which nothing armed another scan and the device stayed
  `present` for the life of the process. A watch event now also sets a
  one-second resettle deadline, and the loop looks once more after the burst
  has finished arriving.
- Opening a node that is being torn down succeeds while every `EVIOC*` ioctl
  on it fails, so the probe returned no kinds — and that zero was written over
  the device, permanently demoting a keyboard to `other`. The poll loop then
  never claimed it again, even once the hardware came back. A probe that
  learned nothing now leaves an existing device as it was.

### When settings are the problem

A document that cannot be read never stops a boot. It is renamed to
`settings.json.broken`, the defaults apply, and `/v1/diagnostics` carries
`settings_document_unreadable` in its enumerated `warnings` — so the way you
find out is the place you already look.

A setting can be perfectly valid and still lock a machine out: `enabled:
false` on its only keyboard. There are two recoveries, and the second exists
because the first needs the API that the setting may have taken away.

```sh
# with the API
curl -X DELETE -H "Authorization: Bearer $TOKEN" .../v1/settings

# without it: boot past the document, leaving it intact
systemctl edit blit    # Environment=BLIT_SETTINGS=ignore
```

`BLIT_SETTINGS=ignore` starts from the defaults and does **not** rewrite the
document, because an operator recovering a machine should not also lose what
was in it. In that mode a `PUT` is refused — a write that could not stick is
worse than a refusal — but a `DELETE` still clears the stored document, so the
recovery can be finished without a shell.

## Restart intents

Two things restart Blit, and which one applies is part of the contract rather
than an implementation detail.

`POST /v1/control/restart` **re-execs the running image** in place. The pid
survives, the port keeps answering, and `tools/tests/restart_gate.sh` asserts
`same_binary=1`. Nothing else is going to restart the process, so it restarts
itself.

A **package install exits** -- non-zero -- and does not exec. The privileged
helper has already run `systemctl daemon-reload` and `systemctl restart
--no-block blit.service`, and that is the only path that can pick up a changed
unit file: credentials, supplementary groups and sandboxing come from the unit,
and an in-process exec cannot change them. The non-zero exit keeps
`Restart=on-failure` as a safety net for the case where the helper dies before
it reaches `systemctl`, because the package must never be installed with the
service down.

Doing both raced, and the race was expensive (issue #63). systemd sends its one
`SIGTERM` to the main pid; a process that execs instead of exiting consumes
that signal and keeps running, so the unit waits out `TimeoutStopSec` -- 90
seconds by default -- and is then `SIGKILL`ed along with **every process in its
cgroup**, including an attached Hibr desktop. The journal signature is:

```
Stopping blit.service ...
blit[<pid>]: blit gateway listening ...          <- the same pid, re-exec'd
blit.service: State 'stop-sigterm' timed out. Killing.
blit.service: Killing process <pid> (blit-server) with signal SIGKILL
blit.service: Killing process <pid> (hibr [attached:) with signal SIGKILL
blit.service: Main process exited, code=killed, status=9/KILL
blit.service: Failed with result 'timeout'
```

Two `listening` lines from one pid is the tell. `tools/tests/update_restart_gate.sh`
asserts the two intents stay distinguishable.

On a host with no SSH, the journal is still reachable: create a terminal
session running `journalctl` with a wide surface and read the cells back off
`GET /v1/surfaces/{id}/cells`. The in-process log ring cannot answer a question
about why the previous process ended, because it died with it.

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

The deterministic pane-target regression is separate from physical acceptance:

```sh
ruby tools/tests/pointer_target_gate.rb
```

It proves that a pointer click selects the session under the pointer with
`routing=pane-hit-test` when no fixed pointer route exists. A fixed
`evdev-pointer` route intentionally overrides hit-testing and is reserved for
dedicated input-probe workloads.

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

## Live experience capture contract

`tools/diagnostics/experience-capture.sh` is the operator boundary for a real
host. It records the deployed API version, display modes, topology, input
devices and routes, terminal-session details, surface summaries, bounded
runtime counters, traces, and post-capture framebuffer images. It discovers
display IDs from `/v1/displays` and surface IDs from terminal-session details
when topology is temporarily missing them, so it is safe for one or many
outputs and for PTY sessions that are still attaching.

Interpret the evidence in this order:

1. `last_render_ms`, `max_render_ms`, `missed_deadlines`, and `loop_gap` decide
   whether the renderer was late; `present_calls` is damage-driven and is not
   itself a vblank rate.
2. `terminal_bytes` and `terminal_reads` prove producer activity.
   `surface_commit_hz`, its window/age/gap fields, `surface_commits`, and
   `frame_commits` apply only to their corresponding producers.
3. `graphics_texture_uploads`, `pixel_frames_accepted`, and
   `pixel_frame_errors` distinguish a Hibr/image transport problem from an SDL
   presentation problem.
4. Aggregate evdev counters plus the per-device entries and input traces
   distinguish no physical input from a wrong device, duplicate device, or
   dispatch problem.
5. Screenshots are visual evidence captured after the timing window; they must
   not be used to infer a frame rate.

An image-only Hibr probe is the control experiment. If it accepts a pixel frame
and uploads a texture while the desktop capture reports zero image activity,
the display server and adapter are working and the desktop is selecting its
glyph fallback/configuration path. That conclusion is evidence-based and does
not require changing Hibr.

# Live Hibr boundary capture

For a real-host report, capture the three layers separately and keep the raw
framebuffer beside the API snapshots:

1. Query `/v1/version`, `/v1/topology`, and `/v1/diagnostics/runtime`.
2. Capture `/v1/diagnostics/screenshots/display-1?cached=1` as BMP and convert
   it to PNG without resizing.
3. Start one ordinary shell surface through `/v1/terminal-sessions`, write a
   known marker, and run `pointer-artifact-capture.sh ... api`.
4. Start the unchanged Hibr `session.hibr` through the same API request and
   capture it with the same pointer path.
5. Run `stty size` in a separate ordinary shell surface using the exact Hibr
   surface dimensions. The reported rows and columns must equal the API
   surface dimensions before a geometry defect can be attributed to Blit.

Interpretation is deliberately layered:

- A clean shell marker plus coherent API pointer traces isolates Blit's
  framebuffer and input composition path.
- `refresh-aware`, the display mode refresh, present timing, and missed
  deadlines are the performance evidence; a static damage-driven terminal is
  not expected to present continuously at the refresh rate.
- If `stty size` matches the surface but an unchanged Hibr desktop occupies
  only part of the captured framebuffer, the defect is in Hibr's desktop
  layout/session state or its content boundary. Do not “fix” that by changing
  Blit's raster geometry or by modifying Hibr.

The 0.1.389 live capture on the real host established this boundary: Blit
reported 1920x2160 at 60 Hz, the control PTY reported 240x135, and the shell
surface rendered cleanly. The unchanged Hibr capture presented coherently with
no missed deadlines but occupied only the upper desktop region, leaving the
lower framebuffer black. The captured artifacts are retained locally under
`diagnostics/live-0.1.389-*` and are not treated as a passing full-desktop
acceptance result. The Hibr-side lifecycle defect is tracked in
[Hibr issue #145](https://git.home.arpa/itdlabs/hibr/issues/145); Blit remains
responsible for preserving the evidence and must not paper over the missing
Hibr rows by inventing terminal cells or changing the output geometry.

The same boundary was reproduced against 0.1.393 on 2026-10-09. The ordinary
shell control returned `stty size` `135 240` and the Hibr capture was saved at
`diagnostics/live-hibr-screenshot-20261009T084834Z/`. The screenshot is the
authoritative visual artifact for this run: it shows the Hibr scene above the
black lower region while the API reports a healthy 1920x2160, 60 Hz SDL output.

## What a capture is, and what its headers mean

`GET /v1/diagnostics/screenshots/{display}` answers from **one** source: the
pixels read during a present, immediately before `SDL_RenderPresent`. There is
no second path. Two older behaviours are gone, because each of them could
answer with something that was never on a screen:

- the request no longer invalidates the output it inspects. The non-cached
  form used to clear `initialized`, forcing a full redraw, so the capture
  showed a frame the request itself caused — and on a surface holding a
  DEC 2026 batch it showed the very content the renderer was withholding.
  `?cached=1` is still accepted and now has no remaining effect;
- the fallback that read the renderer's current back buffer when no capture
  was pending is removed. A request with no presented frame yet waits for one,
  and fails with `screenshot_failed` rather than returning unpresented pixels.

Three headers make a capture checkable rather than merely plausible, and all
three are recorded **when the pixels are read**, not when the response is
written — a slow client can collect a capture several frames later:

| header | meaning |
| --- | --- |
| `X-Blit-Capture-Frame` | the render pass these pixels came from |
| `X-Blit-Capture-Presented` | `true` once that frame's present returned |
| `X-Blit-Capture-Revisions` | per surface, the revision **last drawn into a presented frame** |

The revision is deliberately not each surface's current revision. A surface
whose frame is being held has a current revision ahead of what is on screen,
and reporting that would name a state the pixels do not show. A comparison
between pixels and cell state is therefore only valid when the reported
revision equals the state's revision; `tools/tests/render_state_agreement_gate.rb`
discards a sample where they differ, and `tools/tests/sync_output_gate.rb`
asserts the reported revision is *behind* the live one while a DEC 2026 batch
is open.

### Wake an idle login-manager surface without a keyboard

When the operator cannot access the physical host, use the bounded wake probe:

```sh
BLIT_AUTH_TOKEN=TOKEN \
  tools/diagnostics/wake-and-capture.sh \
  http://HOST:8080 diagnostics/wake-test click
cat diagnostics/wake-test/verdict.json
```

The probe reads the current pointer position, moves it by one pixel inside the
first reported output, and captures the same state and screenshots again. The
optional `click` action sends one button-0 press/release pair, which is the
normal Hibr login-manager wake action for a saver. It never creates a terminal
session, sends a key, changes an input route, or edits Hibr state. A changed
screenshot proves only that the API event reached presentation; a changed
surface revision additionally proves that the terminal producer changed its
surface. Neither result by itself proves that an authenticated Hibr desktop is
running. If the click leaves the saver, the after capture must show the login
screen; if it does not, the screen saver was not proven stopped and the capture
remains the authoritative evidence.
