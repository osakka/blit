# ADR 0098: authoritative session inventory and split evidence

- Status: accepted
- Date: 2026-10-16
- Scope: terminal-session discovery and Hibr split/join diagnostics
- Related: ADR 0082, ADR 0092, ADR 0097

## Context

Blit already exposes separate topology, surface, runtime, trace, and
screenshot resources. The diagnostic capture scripts nevertheless discovered
live sessions by probing a guessed numeric range. That is not an API contract:
login-manager replacement, resumed Hibr desktops, and joined panes can make a
valid session appear outside the range or leave a stale id in the capture.

That ambiguity made it impossible to distinguish these cases from one bundle:

- Hibr authenticated but Blit did not register a primary owner;
- the primary and join were attached to the wrong panes;
- the PTYs produced different surfaces but the compositor presented one;
- a Kitty/half-block image was absent from one pane;
- a frame barrier timed out; or
- the output was stable and only the Hibr-owned desktop placement was wrong.

## Decision

`GET /v1/terminal-sessions` is the authoritative live session inventory. It
returns every currently used session, including `login_manager`, pane and
surface identity, Hibr primary/join binding, terminal capabilities, graphics,
and the current surface revision. Diagnostic clients must use this inventory
and must not infer sessions by probing ids.

The repository also provides a read-only `hibr-split-capture.sh` transaction.
It records the inventory, topology, per-surface summaries, runtime counters,
bounded traces, and screenshot evidence, then emits a verdict identifying the
first missing boundary. It checks the structural contract for the standard
experiment: one primary in the bottom pane, one join in the top pane, one
shared Hibr session name, non-overlapping tiled regions, closed frame batches,
graphics/cell production, and frame-barrier timeouts.

The verdict is evidence, not a screenshot classifier and not a claim that
Hibr's internal display placement is correct. Hibr remains the source of truth
for desktop semantics and placement. Blit reports the pane geometry and the
PTY surfaces it actually received; any mismatch between those and Hibr's
authoritative control-panel state remains an explicit upstream boundary.

## Consequences

- Live captures remain correct when the login manager retires and recreates a
  session.
- The API gains one read-only inventory projection; no second session model is
  introduced.
- A stable screenshot with healthy pane/surface evidence no longer gets
  mislabeled as a frame-rate failure. Conversely, an owner or barrier failure
  is named before visual speculation begins.
- Existing clients that only use `POST /v1/terminal-sessions` are unchanged.
- A server older than the inventory endpoint cannot produce a conforming v3
  diagnostic capture; that is an intentional explicit failure, not a fallback
  to guessed ids.

## Verification

- `hibr_singleton_gate.rb` verifies inventory count, primary discovery, and
  join discovery.
- The TCC build compiles the endpoint.
- `hibr-split-capture.sh` and `analyze-hibr-split.py` provide the live evidence
  transaction and a machine-readable `hibr-split-verdict.json`.
- The existing Hibr viewport and frame-barrier gates remain the deterministic
  fixture proof; the live capture is required for the real SDL/KMS host.
