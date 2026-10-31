# ADR 0083: Blit supervises Hibr's root-owned login manager

- Status: accepted; in use since 0.1.485
- Date: 2026-10-15
- Scope: login-screen lifecycle, privilege boundary, and API control
- Related: Blit #83; Hibr #79, #164, #196, #176

## Context

Blit owns the KMS/SDL presentation and evdev input devices, while Hibr owns
PAM authentication and the desktop or shell selected by the authenticated
person. Hibr already provides `login/login.hibr`, which must run as root for
the PAM session boundary and then use `auth run` to execute the selected
session as that person.

The generic terminal-session API could launch Hibr's login script, but that
would leave the login process looking like an ordinary Hibr session. It would
not define who restarts the screen after logout, how a stale login is stopped,
or how an operator observes the one login manager that is allowed to exist.

## Decision

Blit provides one supervised login-manager resource at `/v1/login`.

- `POST /v1/login` starts the configured Blit login entrypoint in a Blit PTY. A
  request may select a discovered display or pane; absent a selection, the
  first discovered pane is authoritative.
- `GET /v1/login` reports the manager state and the underlying terminal
  session, including its `login_manager` marker.
- `DELETE /v1/login` stops and disables the manager.
- `BLIT_LOGIN_AUTOSTART=1` enables the same manager at service startup.
- The login process is a root child of Blit. Blit does not setuid itself and
  does not read passwords or PAM state.
- Hibr's `auth open`, `auth run`, and `auth close` remain the sole
  authentication implementation. The packaged Blit entrypoint explicitly
  loads Hibr's `sys` module, sources the installed Hibr `login.hibr`, and
  calls its existing `lg_main`; this is required by Hibr's strict root path
  and does not fork or replace login behavior. A separate Blit-owned Hibr
  hook selects the existing Blit desktop entrypoint for `desktop` and the
  person's shell for `shell`.
- When the root login process exits after logout, Blit retires that session and
  starts a fresh login screen after a bounded one-second delay. A failed start
  is reported and retried with a five-second backoff.
- The login manager is not a second desktop owner. It is the one root login
  boundary that launches the user's Hibr desktop or shell; joins remain
  ordinary Hibr join clients under the existing ownership rules.

## Alternatives rejected

1. **Make Blit drop to the logged-in user.** This loses the device-owning
   service boundary and prevents a second clean login without replacing the
   compositor process. It is the X-server model, not the Hibr login model.
2. **Patch Hibr's login screen to know Blit or to auto-load `sys`.** That
   creates a second integration implementation and violates the repository
   boundary. Blit uses a narrow entrypoint shim, Hibr's existing
   `LG_SESSIONCMD` hook, and keeps the Hibr tree untouched.
3. **Have clients launch the login script as an ordinary terminal command.**
   That cannot enforce one manager, fresh-login restart, or an observable
   stop operation.

## Consequences

- A packaged deployment can boot directly to Hibr's login screen while Blit
  retains KMS and evdev ownership.
- The package's `/usr/share/blit/hibr/blit-login.hibr` is an integration shim,
  not a login implementation: it loads `sys` before sourcing Hibr's installed
  script. If Hibr later guarantees that `sys` is loaded before this root path,
  the shim can be removed in a recorded release decision.
- The login endpoint is privileged control and therefore inherits Blit's
  bearer authentication and network-binding policy.
- Root-path PAM/`logind` behavior still requires a real VM or host with Hibr's
  login dependencies. That is Hibr #79's acceptance work, not a claim made by
  Blit's dummy-backend gate.
- Hibr provider mouse restoration, provider reattachment, and two-dimensional
  joined picture propagation remain Hibr-owned concerns tracked separately.

## Verification

`hibr -n hibr/blit-login.hibr` validates the shim syntax, and the package gate
requires the shipped shim to contain the explicit `mod load sys` boundary.
`tools/tests/login_manager_gate.sh` runs the root service under a controlled
`/bin/true` login command, proves the login session is marked and exposed by
the API, proves an exited login is replaced by a new session, and proves the
API stop disables and releases it. It does not claim PAM authentication or a
real Hibr desktop; those require Hibr #79 and the live-host acceptance path.
