# ADR 0130: direct-KMS live output reconciliation

- Status: accepted
- Date: 2026-10-18
- Issue: Blit#100
- Supersedes: ADR 0129's proposed SDL extension for this delivery

The user explicitly requests pure KMS/DRM and direct-backend hotplug. SDL and
Hibr remain unchanged. Backend selection is switched through the authorized
API and existing service configuration, preserving connector/pane intent;
backend-specific transient pane IDs are resolved before restart.

Refactor startup output creation/destruction into the same lifecycle used
for hotplug. A bounded read-only worker observes DRM uevents and periodic
fallback snapshots; connector probing must not run on every rendering turn.
The main thread alone owns modesets, scanout buffers and active output state.
Snapshots are keyed by connector, not enumeration order. Existing outputs,
placements and primary PTYs survive unrelated additions/removals.

Output slots have stable addresses. Page-flip cookies carry an immutable
output generation; late completions cannot mutate a recreated output. Retire
scanout before freeing buffers, quarantine failed teardown, and report bounded
resource exhaustion rather than reusing a live cookie. Failed add/remove work
must not reset or destroy unrelated outputs. A zero-output inventory keeps
the API alive and desired configuration intact.

One gateway reconciliation updates existing display/pane resources and their
revision, retaining session IDs and pane intent on absent outputs. The existing
auxiliary controller handles binding disappearance/return and authenticated
standby startup; there is no second desktop controller or settings store.
Primary loss does not authorize automatic migration. Unknown monitors appear
unconfigured; configured monitors may provision after native readiness.

Acceptance separates deterministic mocked-DRM lifecycle/API checks, compositor
regressions and real scanout on 10.54. Actual second/third monitor unplug/replug
is required before claiming physical hotplug acceptance. This increment targets
connectors on the selected DRM device, not multi-GPU ownership or a universal
high-refresh performance guarantee.
