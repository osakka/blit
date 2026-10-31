# ADR 0153: independent bounded presentation trials

- Status: superseded by ADR 0184
- Date: 2026-10-20
- Superseded by [ADR 0184](0184-sdl-is-removed.md): SDL is removed; native KMS is the only presentation path.
- Tracking: Blit 109/104

SDL initialization runs before Blit's listener. Recovery must therefore not
depend on that listener, event loop or process. Do not overwrite and later
restore the canonical settings: that risks discarding newer configuration.

Use the existing root helper to prepare a frozen, explicitly temporary trial
snapshot, disable trial login, and append one reserved runtime systemd drop-in
which selects that snapshot for the existing blit.service. Credentials, user,
groups, environment and executable remain those of the same service. Normal
saved settings are never changed. Trial configuration endpoints are read-only.

Before activation, arm a separate systemd timer which invokes the same helper
binary in recovery mode. Its unit is outside blit.service and the helper's
service cgroup. Expiry or explicit restore removes only the matching owned
drop-in, reloads systemd, terminates the trial process and starts the original
service. No second API listener, renderer or persistent configuration system.
Reboot clears the runtime override; helper restart restores an outstanding trial.
From 570, the temporary override also sets Restart=no: each trial attempts
startup once rather than repeatedly initializing the GPU after a crash. Normal
service restart policy returns when that override is removed. See ADR 0154.

The existing authenticated API offers GET/POST/DELETE on
/v1/control/presentation-trial. POST selects only software or opengles2 and a
30..120s lease, requires a clean native baseline without authenticated desktop,
and creates no arbitrary root paths/commands. DELETE requires the current
opaque ID; stale IDs cannot affect newer trials. Updates are refused while
recovery is required. Bounded helper commands and socket I/O prevent a stalled
client/command monopolizing the helper. Scheduling failure must not activate
the override. Foreign files/symlinks are never overwritten or deleted.

Test boundary failures, stale restore, lease expiry with an unavailable frontend,
helper restart, unchanged canonical settings, concurrent mutation refusal and
update exclusion before any live trial. Mocked systemd tests are not physical
recovery acceptance. Kernel/GPU D-state hangs or a failed system manager cannot
be guaranteed recoverable by userspace; preserve explicit failed status.

References: systemd-run(1) timer service ownership; systemd.exec(5)
EnvironmentFile ordering. No SSH, Hibr modifications or SDL vendor changes.

Physical evidence: both software and opengles2 thirty-second trials failed
before the listener (journal reports SIGSEGV after both SDL windows exist).
Independent expiry restored ready native KMS, two outputs, revision 237 and
the identical saved document in both cases. This verifies recovery from those
observed crashing startup attempts, not an injected physical blocked call or
kernel hang. The blocked-userspace fixture remains a mocked-systemd test.
API-only oracle: tools/probes/host_presentation_trial.rb. SDL acceptance failed;
recovery success must not be reported as SDL startup/rendering success.
