# ADR 0205: restart after an update at a safe moment

- Status: accepted
- Date: 2026-11-01
- Scope: lifecycle, settings, diagnostics, the performance panel, packaging
- Depends on: [ADR 0008](0008-api-supervised-package-lifecycle.md),
  [ADR 0203](0203-performance-panel.md)

## Context

The package deliberately does not restart Blit from its postinst. An update
through Blit's API (ADR 0008) restarts it once dpkg has finished, and a
plain `apt upgrade` is usually typed into a terminal that runs *inside*
Blit, where a restart would kill the upgrade half done. But after such an
`apt upgrade` the old Blit keeps running until a restart or a reboot, and
nothing says so. On the owner's 10.52 machine 0.3.25 was installed at 13:23
while 0.3.24 kept running for hours.

## Decision

- **Noticing.** Blit checks every 5 s whether the executable it was started
  from has been replaced on disk (`/proc/self/exe` now reads `(deleted)`,
  or the path's inode or mtime changed). The package installs
  `/usr/lib/blit/VERSION`, and Blit reads the installed version from it.
  A restart is pending when the installed version differs from the running
  one. An update through the API sets nothing new: it restarts as before.
- **The policy**, a setting: `server.restart_after_update`, persisted and
  revisioned like every setting.
  - `at-login-screen` (default): Blit restarts itself, through the same
    orderly path as `POST /v1/control/restart`, at the first **safe
    moment**: the login screen is showing with nobody signed in (the login
    desktop is not associated with a user), no terminal session started
    through the API by a user other than the login manager is running, no
    input has arrived for 10 s, and no update through the API is in
    progress. A restart is never attempted twice for one installed
    version: if the new one fails to start, systemd's restart policy is
    what brings Blit back.
  - `manual`: Blit only reports the pending restart.
- **Reporting.** `/v1/diagnostics` gains `update.running_version`,
  `installed_version`, `restart_pending`, `restart_after_update` and,
  while waiting, `waiting_for` (what the safe moment still lacks, e.g.
  `signed-in`, `input`). The performance panel's Blit block shows one line
  while a restart is pending: `update  0.3.27 installed, at login screen`
  (or `restart to apply` under `manual`).
- **Control Panel.** The Blit pane's Server section gains a "Restart after
  update" choice (At login screen / Manual) and, while pending, an info row
  with both versions beside the existing "Restart Blit…".

## Consequences

- An `apt upgrade` takes effect the next time the machine is at the login
  screen, with no user action and nothing lost.
- A machine that never leaves a signed-in session keeps the old Blit until
  someone restarts it; the panel and diagnostics say so.
- `manual` keeps today's behaviour, with the reporting added.

## Verification

- A lifecycle gate: replace the running binary under a mock install root
  (`BLIT_INSTALL_ROOT` for tests) and its VERSION file. Then check:
  `restart_pending` turns true with both versions; under `manual` nothing
  restarts; under `at-login-screen` with a session signed in nothing
  restarts and `waiting_for` says why, and once the login screen is idle
  for 10 s the server restarts once, comes back with the new version, and
  `restart_pending` is false.
- The package artifact gate: `/usr/lib/blit/VERSION` is in the package and
  equals its version.
