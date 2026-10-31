# ADR 0143: Translate initial join placement into Hibr's primary-relative frame

- Status: accepted
- Date: 2026-10-19
- Related: ADRs 0126–0128; Blit #103, #104

## Evidence

The native restart/rejoin fixture fails after the local Hibr installation
updates from 0.99.255 to 0.99.256 (ABI 16 unchanged). Tracing the Blit-owned
placement helper shows `hold move desktop blit-1-p1 -24 0`. The observed
native primary is that upper client at column 128, whereas the associated
login client is at column 0. The move leaves the upper client at column 128
and the relative placement does not confirm.

Read-only inspection of Hibr's `hd_move` contract and `HD_MOVE` handler shows
that arguments are relative to the native primary, not absolute union
coordinates. The Blit helper calculated an absolute login-anchor position
and passed it directly. This is Blit's coordinate translation defect; no
Hibr change or primary-role override is needed.

## Decision

From the same guarded `hold clients` snapshot, obtain the associated login
anchor, target client and exactly one native primary. Calculate the desired
absolute anchor-relative position, then subtract the primary's current
row/column before invoking supported `hold move`. Refuse missing/ambiguous
native state. Keep owner PID/start-time checks and post-move confirmation.

Do not promote a client, assume the login client is the native primary,
change Hibr, reassert confirmed user placements or use a second controller.
The associated login role and independently mutable native primary flag
remain distinct under ADR 0127.

## Verification

Require the native restart/rejoin fixture to pass on installed 0.99.256.
Exercise a nonzero native-primary origin explicitly, verify the resulting
offset relative to the login anchor and verify the primary role is unchanged.
Retain guarded failure diagnostics and the actual-desktop variant. A raw-PTY
diagnostic lacking terminal-query responses was discarded as an invalid
harness, not evidence about Hibr.

The focused fixture passes with an explicitly offset native primary and
verifies that placement does not change that role. The installed-desktop
variant with resize also passes on Hibr 0.99.256/ABI 16: automatic invitation,
confirmed placement, restart/rejoin, preserved user movement, release and
logout cleanup. No Hibr source or installation changes were made.

The fresh full suite passes 107 gates with 7 explicit skips and 0 failures on the
unchanged installed binary. The four-PTY 120-second soak passes 11,504
snapshots and 1,046 resizes. Physical revalidation remains separate.
