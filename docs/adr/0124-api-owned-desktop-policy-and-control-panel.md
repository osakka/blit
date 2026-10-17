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

Settings storage reports clean/pending/error/disabled and its last saved
revision. Replacement must retain the old file until the atomic new write.
A forced save failure returns an error, not a successful persistence claim;
already-applied live changes are not claimed rolled back. Clients must read
state after the error. Ordinary device writes may still coalesce; `persisted`
continues to mean persistence is enabled, not an acknowledgement of durability.

## Remaining stages

1. Verified authenticated desktop association and native standby integration,
   using Hibr's supported per-user interface, never screen/process-name guesses.
2. Stable bindings, explicit intentional release, bounded retries and primary
   fallback/return policy, with observable reasons and no duplicate owners.
3. Hibr-native Blit pane, bounded asynchronous requests, revision conflicts,
   configuration export/import and validation/apply UX; catalogue lifecycle tests.

Hibr already distinguishes standby waiting from deliberate release. Blit must
integrate that boundary, not compete with its automatic joins. Hibr's existing
Displays pane and a Blit policy controller must have explicit ownership of
placement; two writers silently rearranging the same clients are rejected.
No Hibr-core changes, SSH, renderer changes or machine-specific hardcoding.

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
Later stages additionally require login/logout, owner replacement, deliberate
detach, join crash, reconnect, zero/one/multiple outputs and conflict tests.
Physical-host/package acceptance is separate from local fixture acceptance.
