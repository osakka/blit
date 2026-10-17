# Current state and restart point

This is the handoff document for resuming Blit work after a session change. It
is intentionally evidence-led and does not replace the OpenAPI contract,
source, ADRs, or the verification ledger. It tells the next engineer where the
work actually stopped.

## Status at the last review

The current source candidate is `0.1.524`. Hibr source was not modified.

The authenticated live host at `192.168.10.54` reports `0.1.524` with
`backend=drm-kms`, `direct_kms=true`, `video_driver=direct-kms`,
`presentation_sync=drm-page-flip`, and one 1920x2160@60 Hz output. The
persisted 1x2 topology has the supervised login manager in the lower pane and
no surface in the upper pane; this is the controlled single-desktop test
state, not a primary-plus-join acceptance result.

ADR 0108 narrows changed-view composition to surface-local damage while
retaining an atomic page-flip boundary. The live fast-terminal workload then
measured 61.957–63.137 Hz, 14.085–16.892 ms last frame gaps, and a 10.903 ms
maximum KMS present time, with zero new missed deadlines in its measurement
windows. The cumulative counter still contains startup/login-relocation
events, including a 4.646 s maximum historical gap; that is not reported as
motion-pass evidence. Direct-KMS frame-rate counters are now populated by the
same diagnostics path as SDL.

The package identity is recorded in [`releases/0.1.524.yaml`](../releases/0.1.524.yaml).
These are bounded renderer measurements, not visual acceptance of the Hibr
desktop, physical input, wallpaper, graphics protocol, or joins.

### 0.1.524 live login-manager wake evidence

On 2026-10-17, `tools/diagnostics/wake-and-capture.sh` was run against the
live `0.1.524` host while the only Blit session was the supervised Hibr login
manager. A one-pixel API pointer motion was accepted and produced a changing
surface/screenshot, confirming the visible colored lines were an active Hibr
login-manager saver rather than a static framebuffer. An explicit API
button-0 press/release pair then produced two button trace events and a
screenshot containing Hibr's `User name` login box. Hibr's own login source
intentionally keeps the saver behind that box, so the remaining animated
background is expected login-manager composition; it is not evidence of an
authenticated desktop or of a complete desktop render.

The probe deliberately does not create a session, send a keyboard key, alter
input routes, or modify Hibr. Its screenshot and JSON verdict are temporary
live evidence under `/tmp`; the repeatable tool and its interpretation are
tracked in `tools/diagnostics/wake-and-capture.sh` and
`docs/diagnostic-capture.md`.

The live renderer-agreement probe was also corrected to discover the physical
display from topology; its previous `display-1` assumption caused a 404 on the
real `display-93` host. With that correction, six attempts against the active
login-manager saver were all discarded because the 12 MiB screenshot straddled
a changing surface revision. This is inconclusive evidence, not a renderer
pass or fail; a static controlled surface is required for a pixel agreement
assertion.

The required static controlled surface was then run in the free lower pane.
The direct-KMS pixel agreement passed with `TRAIL_PASS blank_cells=84
no_unaccounted_pixels=yes`. The live moving-surface probe then updated a
known block twelve times and passed with
`MOTION_PASS steps=12 accepted=12 discarded=0 direct_kms=true
display=display-93`. These results prove the canonical cell-to-pixel mapping
and bounded damage path for an API-owned surface on this host. They do not
prove Hibr's saver, wallpaper/Kitty composition, desktop windows, or physical
input; those remain separate tests.

### Architecture decision check

There is no accepted decision to deprecate the direct DRM/KMS or non-SDL
backend. ADR 0004 remains accepted and defines direct DRM/KMS as the
correctness-critical terminal presentation path; SDL3 is the adapter for the
optional canvas/effects path. The OpenAPI contract, source, package, tests, and
verification ledger all still describe both paths. Do not remove KMS code or
rewrite those references unless a new, explicitly accepted ADR supersedes ADR
0004. “SDL3/KMSDRM” and “direct DRM/KMS” are distinct paths and must not be
collapsed into one undocumented implementation.

The `0.1.510` package carries the authenticated
`GET /v1/diagnostics/observation` endpoint. It is a schema-first aggregate of
the existing serializers for one operational state dump. The endpoint is
installed and live-verified on `192.168.10.54`.

The exact live presentation-path mapping was revalidated on 2026-10-16:

- SDL/OpenGL means `backend=sdl3`, `video_driver=kmsdrm`,
  `renderer=opengles2`, and `presentation_sync=sdl-vsync`;
- SDL/software means `backend=sdl3`, `video_driver=kmsdrm`,
  `renderer=software`, and `presentation_sync=scheduler-only`;
- direct KMS means `backend=drm-kms`, `direct_kms=true`,
  `video_driver=direct-kms`, no SDL renderer, and
  `presentation_sync=drm-page-flip`.

All three live paths reached `ready=true` on the single 1920x2160@60 Hz
output with empty diagnostic errors and warnings. Focused synthetic and live
composition probes also completed without a focused deadline miss; the
software path had one startup scheduler miss during handoff. The host was then
restored to SDL/KMSDRM + OpenGLES2 with one Hibr primary in the lower pane and
one Hibr join in the upper pane, ready for physical observation.

The product is **not fully physically accepted**. The latest bounded live
capture showed a coherent two-pane Hibr primary/join composition, and the
Kitty image probe completed without rejection or stale-pixel evidence. It did
not exercise a real window drag, physical click/key delivery, or context-menu
placement, so those acceptance claims remain open.

The following narrower claims are supported:

- the server answers `/v1/version` and `/v1/diagnostics/runtime` over TCP;
- a fresh primary plus one `--join` can be created through the API when the
  primary establishes the Hibr hold first;
- the current live renderer matrix completed its focused synthetic workloads;
- the deterministic pointer-damage, motion-trail, wallpaper-stress, and Hibr
  frame-barrier gates pass locally;
- physical acceptance is **not measured**: the latest 20-second capture saw no
  physical evdev motion or button event, so it cannot diagnose real window
  dragging.

These results do not prove that the wallpaper, desktop scene, context menus,
window movement, physical mouse, keyboard, or joined coordinate system works.
An unchanged or incomplete screenshot is a failed visual result even when
timing counters are healthy.

### 0.1.514 performance and login-placement candidate

The source candidate for `0.1.514` retains the bounded integer-scale KMS
graphics path in [ADR 0102](adr/0102-kms-integer-scale-fast-path.md), adds
the persistent CPU scene described in [ADR 0103](adr/0103-kms-cpu-scene-cache.md), and
closes the auto-start login relocation invariant in [ADR 0104](adr/0104-login-target-relocation-and-pane-redraw.md): the session returned by
`/v1/login` is explicitly attached to the requested pane before topology
derives its surface assignment. This targets the observed mismatch where the
login resource named the lower pane while its session occupied the upper
pane. The package is not yet deployed and no live improvement is claimed.

### Latest live read-only boundary

On 2026-10-16, the final live observation query to `192.168.10.54` reported
`0.1.510`, SDL3/OpenGLES2, one 1920x2160@59.99 Hz output, one Hibr primary
surface in the lower pane, and one Hibr join surface in the upper pane. The
bounded final capture used for the renderer record had one inherited
startup missed deadline; the focused motion and composition probes added none.
A later readback showed five cumulative missed deadlines after the longer
service lifetime, while the current frame-rate window reported zero missed
deadlines and no diagnostic errors. This does not close physical interaction
acceptance.

## Non-negotiable claim rule

Do not use “stable”, “working”, “correct”, “complete”, or “fixed” without a
scope and matching evidence. In particular:

1. timing stability means only measured scheduling/presentation behaviour;
2. process stability means only lifecycle state and exit/restart behaviour;
3. visual correctness requires a screenshot or pixel assertion showing the
   expected complete scene;
4. physical input requires evdev traces from the device under test;
5. Hibr interaction requires evidence that Hibr received the event and changed
   its surface, not merely that Blit accepted or forwarded an event;
6. joined-desktop acceptance requires both pane screenshots, graphics state,
   coordinate traces, and a visible window/context-menu interaction;
7. if any required visual or interaction evidence is missing or contradictory,
   the overall result remains incomplete.

Never promote a synthetic API result, QEMU result, or partial screenshot to
physical-host acceptance. Record the exact evidence, limitation, and next
boundary instead.

## Correct live orchestration

The live host currently uses one physical output split into two panes: the
primary is the lower pane and the join is the upper pane. The safe sequence is:

1. query `/v1/version`, `/v1/diagnostics/runtime`, `/v1/topology`, and
   `/v1/terminal-sessions`;
2. remove only exited/stale Blit terminal-session resources;
3. start exactly one primary with the installed Blit Hibr entrypoint and the
   Hibr held-session name;
4. wait until the primary surface contains a real desktop/menu and has a
   nontrivial surface revision;
5. start one `--join` client against the same Hibr held-session name in the
   upper pane;
6. verify both sessions are `running`, both surfaces are distinct, and neither
   contains an exit/error line;
7. capture the screenshot and graphics/session snapshots before testing input;
8. perform physical input capture while a visible window is available.

Do not start a join against a dead or merely stale Blit primary. The observed
failure mode is a join with `hibr: hold: no such session`, followed by retained
old pixels if the exited surface is not removed. Do not interpret those pixels
as a current desktop.

The primary invocation is a normal create request using the installed
entrypoint, for example:

```sh
curl --fail --request POST \
  --header 'Authorization: Bearer TOKEN' \
  --header 'Content-Type: application/json' \
  --data '{"command":"/usr/share/blit/hibr/blit-session.hibr","arguments":["--session","desktop","--name","display-1"],"user":"osakka","display_id":"display-1","pane_id":"pane-display-1-2","surface_mode":"terminal"}' \
  http://192.168.10.54:8080/v1/terminal-sessions
```

The join uses the same command and session name with `--join` and the upper
pane. The exact pane IDs must always come from `/v1/topology`; never invent or
hard-code them in a client.

## Next investigation, in order

The next cycle is a rendering/interaction investigation, not a performance
claim cycle:

1. reproduce the complete expected Hibr scene in one primary pane and capture
   its terminal cells, graphics inventory, filtered traces, and screenshot;
2. determine whether Hibr emitted the expected Kitty payload at all; if it did
   not, stop at the Hibr boundary and do not blame SDL composition;
3. if the payload is present, compare graphics placement/source rectangles and
   scene signatures with the screenshot, first with one pane and then with the
   join;
4. only after a complete static scene is visible, run a physical capture with
   a visible window and correlate evdev motion/button -> Hibr surface revision
   -> render path -> presentation;
5. use the first missing boundary as the defect location and make one surgical
   change;
6. rebuild with TCC, run the focused renderer/Hibr gates, deploy the exact
   package, repeat the same capture, update the ledger, commit, and push.

Do not change Hibr source. If the evidence identifies a Hibr hold, viewport,
picture-propagation, or desktop-semantic defect, record or update the Hibr
issue and keep the Blit diagnosis separate.

## Open-backlog order

The open issues are worked in evidence order, not issue-number order:

1. **#86 — live Kitty wallpaper discriminator.** Capture Hibr's graphics
   decision and Blit's per-session graphics trace on the deployed binary. The
   first missing boundary determines whether the next change belongs in Blit
   or needs a separate Hibr issue.
2. **#84, #85, #87, #88 — renderer/protocol fixes.** These are implemented and
   pushed, but require package deployment and host acceptance before they can
   be closed.
3. **#76, #77, #78 — `console-blit` input/provider boundary.** Exercise only
   after a complete static scene is visible, so input evidence is not confused
   with a missing wallpaper or stale surface.
4. **#79, #20, #21 — hardware/renderer coverage gates.** These need GPU,
   multiple physical outputs, or high-refresh hardware and cannot be closed by
   a local SDL software run.
5. **#22, #27, #60, #64, #82, #83.** These are coverage, workflow, resize,
   persistent configuration, image-build, and login-manager work to schedule
   after the live rendering boundary is established.

This order is a triage record, not a second backlog. Gitea issues remain the
authoritative task records; this document records only the dependency order
and the evidence required to move between them.

## Evidence locations

Temporary captures are outside Git. The latest relevant local captures were:

- `/tmp/blit-clean-state-20261016/` — fresh primary+join screenshot; visually
  incomplete flat-colour scene;
- `/tmp/blit-physical-drag-20261016-174655/` — authenticated 20-second
  physical capture with no evdev motion observed;
- `/tmp/blit-physical-move-20261016-172710/` — earlier no-input capture whose
  screenshot contained the stale ended primary/join failure.

These paths are evidence for the current workspace only. Do not commit them,
their credentials, or generated packages. The durable summary belongs in
[`docs/verification-status.md`](verification-status.md).
