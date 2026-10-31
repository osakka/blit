# ADR 0128: live auxiliary-display Apply contract

- Status: accepted; in use since 0.1.543
- Date: 2026-10-17
- Related: [ADR 0124](0124-api-owned-desktop-policy-and-control-panel.md),
  [ADR 0126](0126-native-login-desktop-association.md),
  [ADR 0127](0127-native-client-pty-input-and-login-owned-joins.md), Blit #99

## Context

0.1.542 stores validated `document.auxiliary_displays` bindings but does not
apply them to a live desktop. Hibr's Displays pane owns current-session
placement after initial join. A live Apply operation therefore cannot reuse a
settings write: doing so would silently persist intent and make the UI unable
to distinguish “use now” from “save as default”.

## Decision

Expose a current-session resource at `GET /v1/auxiliary-displays` and an
explicit replacement operation at `POST /v1/auxiliary-displays/apply`.
Apply's `bindings` array has the same validated `{output, pane_ordinal}` shape
as the saved settings field. An empty array means “no auxiliaries requested
for this desktop generation”. The request replaces the volatile live policy;
it does not modify settings, settings revision, or saved revision. Saving
`document.auxiliary_displays` remains a separate settings operation and does
not implicitly apply it.

The POST is asynchronous and returns an operation identifier plus per-binding
desired/effective state. The GET reports the current operation and its
per-binding state. States distinguish at least: `waiting_authentication`,
`waiting_desktop`, `waiting_output`, `waiting_pane`, `starting`, `waiting_join`,
`joined`, `stopped`, and `conflict`. Every state includes a stable
reason code; unavailable hardware is never silently redirected to the primary
pane. The service reports `joined` only after the exact named client appears
in a fresh native client inventory and the one-time requested placement is
confirmed by a subsequent native observation.

Apply is idempotent for an identical request and generation. Optional
`retry_stopped: true` explicitly retries exited or failed bindings; an
Idempotency-Key protects that action against transport retries. Optional
If-Match checks the live policy revision, independent of settings and status
observations. Replacing the
live policy cancels only Blit-owned standby clients for bindings removed from
the request; it never terminates the authenticated desktop owner. Stale or
failed native queries do not prove logout or absence. A user move after the
initial placement is not reasserted. If a standby process exits during the
same desktop generation, Blit reports `stopped` and does not respawn it
automatically; this conservative rule prevents undoing an intentional Hibr
quit. A new explicit Apply or verified desktop generation may provision it
again. Deliberate Hibr release remains Hibr-owned and is reported as
`released` only when the supported interface proves that state. If it cannot
distinguish release from absence, Blit reports waiting/unknown rather than
inventing that distinction. The current native interface cannot prove release;
0.1.543 reports `waiting_join/native_client_absent_release_unknown` after an
observed attached client disappears while its standby process remains alive.

Each binding resolves its stable connector selector against current output
inventory and its one-based row-major pane ordinal against current pane
topology. Output/pane absence remains a visible waiting state. Duplicate
targets or targets already occupied by a non-managed client are conflicts.
The first implementation must bound concurrent starts/queries, preserve the
frame loop's nonblocking behavior, and use the existing authenticated native
association, Hibr CLI and settings validator. No Hibr source/state changes,
second settings store, shell-script controller, or SSH.

## Acceptance

1. OpenAPI and schema gate pin both operations and their strict response types.
2. Apply changes neither settings bytes nor settings revisions; saving a
   default changes neither current live policy nor active Hibr placement.
3. Red/green tests cover empty/replacement/idempotent requests, authentication
   and owner waiting, output/pane missing and return, occupied panes, start
   failure, exact native join and one-time placement, user-move preservation,
   process exit without same-generation respawn, logout/owner replacement
   cleanup, and revision-independent live Apply.
4. The Hibr Control Panel is not started until the packaged API implements
   these semantics. Physical-host acceptance is separate from local fixtures.

## Consequences

0.1.543 implements the controller and loads saved auxiliary defaults at boot.
An explicit live override lasts until service restart, including later login
generations; settings writes affect the next boot only. Output selectors use
native connector names where reported, otherwise the backend's display name;
ambiguous names conflict. Initial offsets use complete-cell pane geometry
relative to the login client and the outputs' current logical positions.
Pending placement follows current complete-cell geometry, including a font
change during startup. Once confirmed, subsequent user placement is preserved.
An explicit API reassignment of the managed PTY to another Blit pane reports
`conflict/managed_pane_assignment_changed` and is not silently undone.

`auxiliary_policy_gate.rb` tests the controller with synthetic native inventory
and real authenticated credential dropping. `auxiliary_native_gate.rb` uses
installed Hibr standby/hold and both API endpoints in an isolated namespace;
its held workload is sleep, so Enter supplies Hibr's documented join action.
With `BLIT_NATIVE_AUXILIARY_DESKTOP=1`, the same gate instead runs the installed
desktop and proves its automatic invitation without injected Enter. Both modes
cover native movement, drop, quit and cleanup. The native observer gate also
tests the placement helper and stale-owner rejection. Physical multi-output
acceptance remains a separate delivery boundary.
