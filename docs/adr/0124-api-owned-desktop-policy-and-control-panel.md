# ADR 0124: API-owned desktop policy and a catalogue control panel

- Status: accepted; delivery is staged, not complete
- Date: 2026-10-17
- Issue: [Blit #99](https://git.home.arpa/itdlabs/blit/issues/99)
- Related: ADRs 0067, 0089, 0122; native association issue #96
- Panel delivery: [hibr-apps #1](https://git.home.arpa/itdlabs/hibr-apps/issues/1)

## Decision

Blit owns desired output/pane/session policy in its existing settings resource.
The Hibr Control Panel is an asynchronous API client, distributed from
`itdlabs/hibr-apps`, not a second settings store or lifecycle controller.
Configuration must distinguish intent, effective assignment and durable save
status. An unavailable resource does not erase the user's preference.

The user confirmed explicit persistence on 2026-10-17: actions in Hibr's
existing Displays pane change the current session only. They do not silently
rewrite Blit's saved startup intent, and Blit must not continually undo them.
The proposed control panel distinguishes Apply (live state) from Save as
default (persistent intent). These are UX semantics, not additional endpoints
already implemented. Automatic remembering may be added later as a reversible
policy through this same store/controller, not a parallel implementation.
Reverting configuration does not promise to resurrect terminated processes.

Stage 1 adds `document.login`: optional `enabled`, `preferred_pane_id` (a pane
ID or null for automatic selection), and `fallback` (`first_available` or
`wait`). Missing properties retain boot defaults. This section is explicitly
startup-applied: a settings replacement must not terminate an authenticated
desktop or relocate it behind the operator's back. Effective policy and a
restart-required flag are returned. `/v1/login` remains the existing live
control boundary; its operations do not implicitly persist configuration.
Absent preferred panes are accepted configuration. `wait` does not create a
login elsewhere; `first_available` may select a fallback without rewriting
the preference. This is login startup selection, not active-primary failover.

IDs in this first stage refer to current backend pane identities. They are
not portable monitor identities: stable output selectors and logical pane
names are required before general hotplug provisioning can be claimed.

Auxiliary startup intent is stored in the same document as
`auxiliary_displays`, a list of unique `{output, pane_ordinal}` bindings.
`output` is the stable connector selector, and `pane_ordinal` is one-based,
row-major within that output's configured 1x1–2x2 pane grid. A configured but
currently absent output remains saved and dormant. This document field is
intent only: saving it does not move the current Hibr desktop. Hibr Displays
remains the live session owner; Blit's eventual standby reconciler may apply
the configured initial placement once when a new standby first joins, but
never undo later user moves. Current-session Apply and durable Save remain
separate actions.

Settings storage reports clean/pending/error/disabled and its last saved
revision. Replacement must retain the old file until the atomic new write.
A forced save failure returns an error, not a successful persistence claim;
already-applied live changes are not claimed rolled back. Clients must read
state after the error. Ordinary device writes may still coalesce; `persisted`
continues to mean persistence is enabled, not an acknowledgement of durability.

The 0.1.542 settings contract implements validation and durable storage of
`auxiliary_displays`; it does not start clients or apply layout. An absent
array means no desired auxiliaries. A present empty array explicitly clears
the desired set. Invalid replacements are rejected before current or durable
settings change. This is still a staged delivery, not completion of the
standby lifecycle below.

## Remaining stages

1. Verified authenticated desktop association and native standby integration,
   using Hibr's supported per-user interface, never screen/process-name guesses.
   The 0.1.540 join path supplies the association and per-client lifetime
   boundary; 0.1.542 adds persisted auxiliary intent. The next implementation
   starts a named `desktop --standby` client only after that association is
   fresh. The standby runs as the authenticated desktop user and occupies its
   configured Blit pane.
2. Bind each auxiliary intent to a stable output selector and pane ordinal;
   do not persist a transient terminal-session ID or enumeration-only display
   ID. Apply the requested Hibr row/column once, after the named standby first
   appears in the supported native `hold clients` result. A later Hibr Displays
   move belongs to the current session and must not be continuously undone.
   Hibr's standby process owns automatic join, deliberate release, and an
   explicit user reattach. Blit must not restart a standby that the user quit,
   or force a released display back into the desktop. A new authenticated
   desktop generation may provision its configured standby again.
3. Report desired and effective state separately, including waiting for
   authentication, waiting for a pane, starting, joined, released, stopped,
   and conflicts. Missing output/pane targets wait or report a conflict; they
   never fall back onto the primary pane. Apply and Save as default remain
   distinct API operations so a live Hibr Displays change is not persisted
   without an explicit save.
4. Build the Hibr-native Blit pane in `itdlabs/hibr-apps`, with bounded
   asynchronous requests, revision conflicts, configuration export/import,
   and validation/apply UX; then run catalogue lifecycle tests.

Hibr's documented standby registry distinguishes waiting from deliberate
release while the standby process lives. Blit integrates that behavior through
the authenticated per-user Hibr process and native hold interface; it does not
read Hibr's private state files. Initial placement is a one-shot operation.
After that, Hibr's Displays pane owns session placement unless the user
explicitly applies a different Blit policy. Two writers silently rearranging
the same clients are rejected. No Hibr-core changes, SSH, renderer changes or
machine-specific hardcoding.

## Alternatives rejected

- A startup script: cannot reconcile later changes or report reliable state.
- Panel-owned persistence: the desktop would require a running panel to recover.
- Joining before authentication or as root: wrong user/session boundary.
- Persisting transient process/session IDs: restores unrelated resources.
- Claiming all settings apply atomically: existing device writes may refuse
  independently; transactional validation/apply is a separate staged contract.

## Acceptance

Stage 1 requires red/green tests for save errors preserving the old document,
invalid policy leaving state untouched, restart precedence, missing target with
both fallback policies, reset/ignore recovery and preference versus assignment.
Later stages additionally require login/logout, owner replacement,
deliberate detach and explicit reattach, standby quit, join crash, reconnect,
zero/one/multiple outputs, missing stable bindings, revision conflicts, and
proof that a user move is not undone. Physical-host/package acceptance is
separate from local fixture acceptance.
