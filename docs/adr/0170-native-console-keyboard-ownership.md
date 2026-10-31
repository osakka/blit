# ADR 0170: native KMS owns its Linux VT keyboard

- Status: accepted; in use since 0.1.587
- Date: 2026-10-21
- Tracking: [Blit 117](https://git.home.arpa/itdlabs/blit/issues/117)

Native KMS reads evdev without disabling the kernel VT keyboard handler.
The packaged service has null stdin and no controlling terminal; SDL's
`/dev/tty` keyboard muting cannot repair native KMS, or a daemon with no VT.

Reserve an unused VT with VT_OPENQRY, keep it open, register VT_PROCESS and
activate it with a bounded wait before native presentation. Remember the
previous VT and return there on exit only when Blit's VT is still active.
Never borrow the active login VT: 586 passed a getty-free VM but failed on
10.54 when getty restarted, invalidating the old descriptor (EIO) and restoring
K_UNICODE. The dedicated VT must survive a hangup of the original login VT.
A single /run/blit/console.lock prevents competing Blit VT owners. Native
input health periodically checks the actual descriptor/mode and stops on
lost isolation; a registered owner is not proof of a usable descriptor.
If the reserved VT itself is hung up, restoration must reopen that same VT;
both the owner's and recovery child's inherited descriptors can be invalid.
The recovery path must never substitute the currently active console.

Use Linux KDSKBMODE/K_OFF on the owned VT, not EVIOCGRAB on the existing
per-role evdev descriptors. K_OFF covers all keyboards, including hotplug,
without starving another reader of a combined keyboard/pointer node. Keep
API injection independent. This is keyboard isolation, not pointer/gamepad
exclusivity: a separately running gpm is not covered by this decision.

Native KMS must register VT_PROCESS release/acquisition signals, stop physical
input delivery and relinquish DRM master before acknowledging release, and
reacquire/repaint before accepting physical input. Ctrl+Alt+Fn must request
VT_ACTIVATE explicitly because K_OFF disables the kernel's normal switch
chords. Never consume another application's VT_PROCESS or graphics VT.

Keyboard mode, display mode and VT mode must be restored on normal exit and
fatal process death. A separate recovery process observes process death;
fatal-signal handlers alone cannot cover SIGKILL. A ten-second main-loop
heartbeat timeout kills the owner through its pidfd and restores the console.
The existing synchronous privileged package-install exchange gets a bounded
120-second lease, reset to 10 seconds on return. It must not be killed merely
because a valid dpkg install exceeds the interactive heartbeat budget. The
watchdog is never disabled; expiry still kills/restores the owner. Diagnostics
report the selected timeout. Tests hold an installation lease beyond 10 seconds
and separately verify the ordinary stopped-owner watchdog.
Preserve the kernel SysRq path (no evdev grab), document its availability, and
retain the independent service/updater stop path. This process-level watchdog
does not cover hardware/kernel hangs or killing the recovery process too.

SDL's own VT handling must not have a competing owner. Native support does
not establish SDL daemon acceptance. Keep 117 open until the relevant real
kernel VT/input and deployed backend lifecycle tests pass. Do not deploy
untested console ownership changes to the operator's active desktop.

References:

- [Linux VT keyboard handler](https://github.com/torvalds/linux/blob/master/drivers/tty/vt/keyboard.c)
- [VT ioctls](https://man7.org/linux/man-pages/man2/ioctl_vt.2.html)
- [kmscon VT handling](https://github.com/Aetf/kmscon/blob/master/src/uterm_vt.c)
- Pinned SDL: `vendor/SDL3/src/core/linux/SDL_evdev_kbd.c`.
