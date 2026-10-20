# ADR0167: shared user-session sockets and bounded service retries

- Date: 2026-10-20
- Status: implemented for candidate0.1.583; physical acceptance pending
- Tracking: [Blit115](https://git.home.arpa/itdlabs/blit/issues/115),
  [Hibr320](https://git.home.arpa/itdlabs/hibr/issues/320)

Blit starts ordinary authenticated user desktops. A systemd private `/tmp`
hides their per-UID Hibr hold/control sockets from shells outside the service,
potentially allowing a second desktop where the first cannot be discovered.
Disable `PrivateTmp` for the display service. Keep the existing root profile
and authenticated API boundary (ADR0065), not a second privilege policy.
Hibr remains responsible for its socket authentication/ownership. Sharing a
namespace does not grant other users permission to use those sockets.

This does not move update staging back into `/tmp`: `/var/lib/blit/updates`
remains the transaction boundary (ADR0008). Default Hibr diagnostics must not
use predictable shared temporary filenames. For Hibr processes without an
explicit `DT_LOG`, allocate a random0700 directory as the final session user
after credential drop, then use its `desktop.log`. Respect explicitly supplied
log paths. Session directories/logs are retained for diagnosis; there is no
new automatic retention/deletion promise. Native PAM/auth launchers may clear
the environment and use Hibr's normal per-user state path instead.

Use `Restart=on-failure`,10 seconds between retries and at most3 starts in
60 seconds. A persistent start failure stops in an inspectable failed state;
after correction an operator can reset the failure and explicitly start it.
Do not gate on `/dev/dri/card0`: connector/card numbering is not stable and
Blit's API must support headless diagnosis. No reboot or tight retry loop.

Local real-systemd tests verify same-UID UNIX-socket visibility, with the old
private namespace as a negative control, and actual failed-state transition
after3 starts using the shipped restart values. Unit syntax validates.
Local API tests verify unique0700 default log directories, UID ownership and
explicit path preservation. Package gate checks the installed unit policy.

The reported stopped root login descendants remain undiagnosed. The Blit
shim contains no background pipeline; stock login/auth is Hibr code, left
untouched. Hibr320 requests help tracing ancestry/job control, not a claim
that Hibr is proven faulty. Do not resume/kill those children speculatively.
Changing the service namespace requires a service restart; existing processes
cannot be migrated by `daemon-reload`. Preserve one desktop owner, inspect
ordinary-shell socket visibility and verify primary/joins after rollout.
