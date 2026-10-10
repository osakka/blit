# ADR 0063: A desktop's cells come from its owner, and a desktop that never paints says so

- Status: accepted
- Date: 2026-10-10
- Scope: terminal output onto an owned surface, Hibr reattachment, session health

## Context

[ADR 0057](0057-owned-surfaces-are-not-interactive-terminals.md) established
that a surface with a registered owner is not an interactive terminal from the
point of view of **physical input**, and closed that route. It deliberately
left the application-requested PTY paths open, and it did not consider the
output direction at all.

The output direction is the same collision. A Hibr desktop reattached with
`--resume` after a Blit restart came back blank and reported itself healthy
(issue #32): 32,400 cells, owner registered, `frame_open` false, every status
check green, screen black.

The arithmetic in that report is what identified the mechanism. The surface was
at revision 10, and a revision counts one accepted cell request
([ADR 0058](0058-a-revision-counts-accepted-writes.md)); the request bound
admits roughly 43 cells. Ten requests cannot carry 32,400 cells. So the content
did not come from the owner. It came through the session PTY and the terminal
engine, which advances the revision once per read batch: `hold attach` fans the
held desktop's terminal stream out to the reattaching client, Blit rendered
that stream into a surface that already had a registered owner, and the result
was whatever the stream happened to contain — blanks.

Two faults, not one:

1. **The stream authored an owned surface's cells.** Even without
   reattachment, a desktop that writes anything to its stdout has it rendered
   over the desktop its owner is drawing.
2. **Nothing said the desktop was unpainted.** `running` with a registered
   owner and a full cell count is indistinguishable from a working desktop,
   which is also how a real measurement was misread once: the first reading
   after a deploy showed zero revisions a second on a blank screen, which
   looks exactly like a frozen desktop.

## Options considered

1. **Leave the stream and have the owner repaint on attach.** The adapter
   already resets its shadow in `cb_open` so its next frame is a full send —
   but an attaching client is relaying a stream, not drawing a desktop, so
   there is no next frame. The trigger would have to come from Hibr, and the
   collision would still be there for any desktop that writes to stdout.
2. **Fail a desktop that never paints**, the way an owner that never registers
   is already failed. Rejected: a slow desktop is still a desktop, and shooting
   a working screen for being slow is worse than the blankness it prevents.
3. **Refuse terminal output into an owner-authored surface, and report an
   owner that never paints.** Chosen.

## Decision

- **A session with a Hibr desktop role does not author its surface from its
  PTY.** The gate is `session_surface_is_owner_authored`, the same predicate
  ADR 0057 chose: the desktop *role*, set at session create from the launch
  arguments, not `hibr_owner_registered`, which arrives asynchronously. Gating
  on the role closes the startup window, which is exactly when a stray write
  does the most damage.
- **The bytes are not discarded.** They still reach the `terminal_output`
  stream, so a client that wants the held session's replay can have it. What
  changes is that they no longer become cells.
- **The owner's paint activity is reported.** `hibr_attachment.owner_writes`
  counts cell requests accepted for that session's surface, and
  `owner_painted` is the boolean a status check wants.
- **A desktop that holds a registered owner for ten seconds without a single
  accepted cell request is reported** as `hibr_desktop_unpainted`, in the
  runtime log and in `/v1/diagnostics` `errors`. Ten seconds is generous on
  purpose: a fresh desktop paints within one, and the cost of being wrong is a
  false alarm about a working screen. It is **not** fatal.
- `--resume` is recorded for the launcher that is actually deployed.
  `hibr_resume_requested` was only parsed when the command was literally
  `desktop`, and the deployed launcher is `blit-session.hibr`, so a resumed
  desktop could not report itself as one — the single fact a blank
  reattachment most needed to state.

`health.ready` is untouched: it describes the display backend, and conflating
it with a desktop's content would make it mean two things.

## Consequences

A resumed desktop is now blank **and says so**, where before it was blank and
healthy. That is the whole of what Blit can decide on its own.

The other half belongs to Hibr and is cross-filed. `hold attach` hands an
attaching client the held session's terminal stream, which is meaningful for a
terminal client and not for one whose display is a provider: the provider has
no frame to send, and the stream it is given cannot become one. Until that is
resolved, the supported recovery after a Blit restart is a **fresh
`--session`**, which draws correctly, and `--resume` will report
`hibr_desktop_unpainted` rather than pretending.

A desktop that legitimately produced terminal output and relied on Blit
rendering it would lose that. Nothing does: a desktop's content arrives over
the adapter, and its stdout is diagnostics.

## Evidence

`tools/tests/desktop_repaint_gate.rb`, which needs no Hibr. A fixture named
`blit-session.hibr` — the basename is how Blit recognises a desktop — records
the credentials Blit puts in its child's environment, then emits a clear, a
home and forty lines, which is the shape of a reattachment replay. A stub
owner registers through `POST /v1/terminal-sessions/{id}/hibr-owner` and draws
nothing.

ADR 0057 declined a stub owner for its own evidence, because what mattered
there was the absence of echo on a surface an owner is *actively drawing*.
Here the stub is the subject: the question is what Blit does when an owner
registers and then draws nothing.

Against the previous binary the gate reports, in order: the PTY authored
**231 cells** of an owned surface, `--resume` was not recorded, `owner_painted`
is not reported at all, the unpainted desktop was never reported in
diagnostics or in the runtime log, and an accepted owner write was not
counted.
