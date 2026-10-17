# ADR 0127: native client PTYs and login-owned joins

- Status: accepted; join boundary increment, not saved automatic policy
- Date: 2026-10-17
- Issues: Blit #96, #99; follows ADRs 0122, 0124–0126
- Supersedes: ADR0080's horizontal slot and primary-input projection

## Evidence and decision

The revised join-input regression fails against0.1.539: a click targeting
session2 is forwarded to session1 at column129, instead of local column1.
This is the obsolete recognized-launch path, not a Hibr rendering defect.
Hibr's installed native hold client already rewrites SGR positions using its
own current row/column. Blit must send pointer, wheel and keyboard input to
the hit-tested/focused client's own PTY, without a union offset or a second
route to the primary. Remove the old projection rather than retaining another
mode switch. Each client also keeps its own output surface and graphics.

Use ADR0126's fresh exact native login association as an eligible owner for
recognized API joins. Authentication alone, expired observation or a shell
does not qualify. Project the associated login's ownership role in the API
without setting external-adapter ownership flags. This role identifies the
desktop association, not Hibr's independently mutable primary-display flag
(that flag remains `login.desktop.client.primary`). The presentation group
includes the associated login and its recognized joins.

For login-owned joins, derive the child's local UID/GID from the verified
identity when `user` is omitted. Reject an explicitly different identity.
Account lookup and supplementary-group initialization for the derived user
happen in the forked child, not the frame loop. Clear inherited environment,
re-establish native terminal defaults and HOME/USER/LOGNAME/PATH, and do not
inherit Blit's API token. Explicit API environment remains operator-authorized.
Require a free logical pane; never cover the login or another client.

`CreateTerminalSession.hibr_join_position` optionally names signed native
row/column coordinates for a one-shot join through the Blit session launcher.
The launcher calls supported `hold attach` at those coordinates and never
moves the primary. Without placement, native Hibr defaults apply. Later Hibr
Displays-panel changes remain session-only; there is no continuous reassertion.
Native `--standby`/`-w` is classified as a join, not another primary; explicit
one-shot coordinates cannot be combined with standby, whose current native
join policy owns its attachment. Saved-policy standby placement integration
is a subsequent controller boundary, not falsely implemented by this field.

Bind login-owned clients to the login generation and observed native owner
PID/start-time. Logout/replacement or a newly verified different owner retires
only those client processes. A temporary query error is not proof of logout.
Retirement signals/reaps asynchronously; it never signals the held desktop,
waits for it in the frame loop or silently follows a different authenticated
session. This is cleanup, not a retry/reconnect controller.

## Validation and remaining delivery

The raw-PTY API gate verifies hit-testing/local coordinates and distinct
surfaces. Installed native Hibr CLI in an isolated namespace separately proves
two67x240 clients form a134x240 union: lower input gets row+67 exactly once,
upper input stays local, and button/release/modifier/wheel/keyboard bytes are
preserved. The credential-dropping login fixture tests recognized joins,
identity/pane/placement rejection, token removal and logout cleanup. These
are not PAM desktop interaction or physical acceptance.

The requested deliverable remains saved API-managed auxiliary policy:
authenticate, associate, provision native standby clients, apply initial
placement once, preserve deliberate release and user rearrangement, then
reconcile crash/output/login changes. Reuse the corrected native path and
existing settings/controller, not an external script, manual operator recipe
or another desktop owner. General stable output binding and fallback/return
policy remain staged under ADR0124. No Hibr-core changes or SSH.

Primary implementation reference inspected read-only:
`hibr/mods/hold/mouse.c` (`hd_mrewrite`/`hd_mtrans`) and
`hibr/programs/desktop/wm/{hold,standby}.hibr`.
