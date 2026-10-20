# ADR0170: native KMS owns its Linux VT keyboard

- Date: 2026-10-21
- Status: implemented in candidate0.1.586; native Linux/QEMU acceptance passed,
  physical deployment and SDL daemon acceptance pending
- Tracking: [Blit117](https://git.home.arpa/itdlabs/blit/issues/117)

Native KMS reads evdev without disabling the kernel VT keyboard handler.
The packaged service has null stdin and no controlling terminal; SDL's
`/dev/tty` keyboard muting cannot repair native KMS, or a daemon with no VT.

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
120-second lease, reset to10 seconds on return. It must not be killed merely
because a valid dpkg install exceeds the interactive heartbeat budget. The
watchdog is never disabled; expiry still kills/restores the owner. Diagnostics
report the selected timeout. Tests hold an installation lease beyond10 seconds
and separately verify the ordinary stopped-owner watchdog.
Preserve the kernel SysRq path (no evdev grab), document its availability, and
retain the independent service/updater stop path. This process-level watchdog
does not cover hardware/kernel hangs or killing the recovery process too.

SDL's own VT handling must not have a competing owner. Native support does
not establish SDL daemon acceptance. Keep117 open until the relevant real
kernel VT/input and deployed backend lifecycle tests pass. Do not deploy
untested console ownership changes to the operator's active desktop.

References:

- [Linux VT keyboard handler](https://github.com/torvalds/linux/blob/master/drivers/tty/vt/keyboard.c)
- [VT ioctls](https://man7.org/linux/man-pages/man2/ioctl_vt.2.html)
- [kmscon VT handling](https://github.com/Aetf/kmscon/blob/master/src/uterm_vt.c)
- Pinned SDL: `vendor/SDL3/src/core/linux/SDL_evdev_kbd.c`.
