# ADR 0126: observe a named native login client before join orchestration

- Status: accepted
- Date: 2026-10-17
- Issues: Blit #96, #99; follows ADRs 0122, 0124, 0125

## Decision

Post-authentication UID/GID does not identify a Hibr held owner or its attached
client. Give the packaged login desktop a unique Blit-owned client name through
Hibr's existing `--name` argument. Pass the name through the root-owned hook
arguments, not the environment cleared by authentication. Custom hooks remain
unchanged. Names are transient bindings, never saved configuration.
This stage observes the packaged `desktop` session in Hibr's default per-user
hold namespace. Custom PAM-provided TMPDIR namespaces are not inferred; they
need an explicit future namespace contract, not process-environment scraping.

Observe Hibr's supported `hold list` and `hold clients` interfaces as the
verified user, through one Blit-owned Hibr helper. Associate only the exact
named client in session `desktop`. Check the reported program PID against
kernel UID and start-time identity, and validate the native client rectangle
against its union and the login terminal's grid. Do not infer a client from
being the only primary, from a process name, or from rendered pixels.

One bounded child performs account/group lookup and native queries outside
the presentation loop. It has a clean environment, dropped credentials, no
inherited device/listener descriptors, no API token, no terminal surface and
no control of input or placement. The main loop drains a bounded pipe without
blocking, imposes a two-second deadline and polls no more than once per second.
Reap cancelled workers asynchronously; never wait indefinitely in rendering.
Discard a result if its login-session generation no longer exists. Shutdown
and login destruction cancel the worker without signalling the held desktop.
The main select deadline includes query scheduling, cancellation reaping and
the hard timeout even on an otherwise completely idle server.

The `desktop` object in `GET /v1/login` is a read-only association projection, not a second
desktop state owner. It exposes inactive/waiting/ready/error, reason, query
activity, timestamp, native owner identity and the named client's rectangle
and primary flag. Missing authentication and shell selection are inactive.
Missing owner/client is waiting; failed or malformed queries are errors.
Ready observations expire after three seconds; no stale result is authority
for launching a join. Hibr remains authoritative for placement and primary
selection; observation never moves, promotes, detaches or starts a client.

This increment deliberately does not set the obsolete Blit desktop-role
flags, which still trigger horizontal input projection. Retiring that path
and publishing native roles for all primary/join clients must be one coherent
follow-up before automated joins use those existing API launch flags.

## Consequences and validation

The user must log in through the newly named hook before association can be
verified. Do not retrofit a name onto a live client or restart an authenticated
session without safe clearance. A valid user handoff alone remains insufficient.

Test the helper against real Hibr hold sessions with unique fixture names.
Controller tests cover unauthenticated/shell gating, exact client matching,
geometry and UID rejection, bounded output/deadline, native-owner disappearance,
replacement/logout cancellation and no public role/input changes. Fixtures
must distinguish native CLI evidence from synthetic worker-result tests.
Physical acceptance requires the installed named login hook and helper on
the authorized host, separately from local fixture acceptance.

Rejected: reading private Hibr wire formats, raw socket paths, changing Hibr,
running `hold list` synchronously in the frame loop, guessing the primary
client, or introducing another lifecycle controller/settings store.

Kernel process identity uses [proc_pid_stat(5)](https://man7.org/linux/man-pages/man5/proc_pid_stat.5.html)
and [proc_pid_status(5)](https://man7.org/linux/man-pages/man5/proc_pid_status.5.html).
