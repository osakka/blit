# Current state and restart point

This is the handoff document for resuming Blit work after a session change. It
is intentionally evidence-led and does not replace the OpenAPI contract,
source, ADRs, or the verification ledger. It tells the next engineer where the
work actually stopped.

## Status at the last review

Blit `0.1.507` is deployed on the authorized host at `192.168.10.54` and
reports API version `0.1`. The matching package identity is recorded in
[`releases/0.1.507.yaml`](../releases/0.1.507.yaml). Hibr source was not
modified.

The product is **not visually accepted**. The latest authenticated screenshot
capture showed a coherent flat-colour scene with the menu/status line but no
Kitty graphics in either live session. Earlier captures also showed a stale
surface containing `[desktop ended, status 0]` and a failed join containing
`hibr: hold: no such session`. These are live failures, not acceptable desktop
output.

The following narrower claims are supported:

- the server answers `/v1/version` and `/v1/diagnostics/runtime` over TCP;
- a fresh primary plus one `--join` can be created through the API when the
  primary establishes the Hibr hold first;
- API-injected pointer stress reached 600 input events and 531 presentations
  with zero new missed deadlines in that interval;
- the deterministic pointer-damage, motion-trail, wallpaper-stress, and Hibr
  frame-barrier gates pass locally;
- physical acceptance is **not measured**: the latest 20-second capture saw no
  physical evdev motion or button event, so it cannot diagnose real window
  dragging.

These results do not prove that the wallpaper, desktop scene, context menus,
window movement, physical mouse, keyboard, or joined coordinate system works.
An unchanged or incomplete screenshot is a failed visual result even when
timing counters are healthy.

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
