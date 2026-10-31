# ADR 0125: private post-authentication identity handoff

- Status: accepted; in use since 0.1.538
- Date: 2026-10-17
- Issues: Blit #96, #99; follows ADR 0124

## Boundary and decision

Hibr's root login manager drops credentials and clears the environment before
executing `LG_SESSIONCMD`. An inherited API token is therefore neither a
reliable nor an appropriate mechanism for identifying the authenticated user.
Blit must not infer authentication from terminal output, process names or an
untrusted TCP client's claimed PID. Hibr core remains unchanged.

For Blit's default session hook only, create a private UNIX datagram socketpair
per login-manager terminal. The receiving end is nonblocking and close-on-exec.
Only that login child's sending descriptor survives exec; its numeric value is
passed as an internal hook argument through the existing `LG_SESSIONCMD`.
This is a private inherited capability, not a filesystem listener or bearer
token. Custom session commands remain unmodified and report unavailable.

The Blit-owned hook sends exactly one selected session kind (`desktop` or
`shell`) after Hibr's authentication boundary. Linux `SO_PASSCRED` supplies
per-message `SCM_CREDENTIALS`, rather than socketpair-creation credentials.
Blit requires a non-root sender descended from this login-manager process,
rejects missing/truncated/invalid credentials or payloads and closes the
receiver after one decision. The hook waits for a bounded acknowledgement
(five-second socket receive timeout), closes its descriptor, then executes
the selected session. Failure must leave ordinary login usable, but must not
enable join automation. Root and the trusted installed login/hook are in the
trust boundary: this is not independent proof of PAM's internal decisions.

No descriptors received through `SCM_RIGHTS` may leak. The receiver discards
and closes such descriptors and rejects the message. Other session execs do
not inherit the receiving capability. The hook's sender is closed before
desktop/shell exec so applications and detached desktop owners do not retain it.

The `identity` object in `GET /v1/login` reports waiting/verified/rejected/unavailable with
kernel UID/GID/PID and selected kind only after verification. No active login
means unavailable. The identity belongs to the current supervised login
terminal and is discarded when it is destroyed/replaced. It is explicitly
not a live desktop-owner PID, desktop readiness, or proof the executable has
finished starting. No saved settings contain transient identities.

## Consequences and next boundary

The main loop services one bounded message, without blocking, NSS lookups or
renderer changes. Invalid handoffs cannot retry themselves into acceptance.
An unavailable or rejected handoff prevents future automatic joins, not the
user's desktop. Existing API authentication remains unchanged; there is no
new public identity-registration endpoint or admin-token exception.

Native per-user owner readiness and standby/release reconciliation remain a
separate increment. Do not set legacy desktop-role/projection flags merely
because the authenticated user is known. The primary may be a shell.

## Validation and alternatives

Tests must exercise real Hibr descriptor inheritance across a clean environment,
actual UID/GID dropping, both session kinds, root/invalid/oversized rejection,
custom-command compatibility, descriptor closure, single-use acceptance and
login replacement. Such a fixture does not prove actual PAM authentication;
physical-host authentication is separately recorded after a user login.

Rejected: scraping a username from a screen; predictable session tokens;
public PID registration; `SO_PEERCRED` on a root-created socketpair; modifying
Hibr; adding a second controller or privileged network listener.

Reference: [Linux unix(7)](https://www.man7.org/linux/man-pages/man7/unix.7.html),
`SO_PASSCRED`, `SO_PEERCRED` and `SCM_CREDENTIALS` semantics.
