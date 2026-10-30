# ADR 0066: the terminal names no file Blit opens

- Status: accepted
- Date: 2026-10-11
- Scope: Kitty graphics transmission media (`t`), terminal-driven filesystem
  and shared-memory access

## Context

The Kitty graphics protocol's `t` key selects how the payload is delivered:

- `t=d` — direct, the base64 payload is the pixels. The default.
- `t=f` — the payload is a *path*; the terminal opens and reads it.
- `t=t` — the payload is a path to a temporary file; the terminal reads it and
  is then required to **delete** it.
- `t=s` — the payload names a POSIX shared-memory object; the terminal opens
  it with `shm_open` and is required to **unlink** it.

Issue #14 filed this because `terminal_apply_kitty` did not read `t` at all, so
every transmission was treated as `t=d` and a path was decoded as pixel data.
That is the dangerous shape: with dimensions that happened to match the path
length it would have produced a garbage image rather than an error. The parse
and refusal landed with ADR 0060's placement work; this record exists because
#14 asked for the *policy* to be decided deliberately rather than inherited
from whatever the code happened to do.

The reason this is a security decision and not a feature-completeness one is
the channel. A terminal escape sequence is not an authenticated request. Any
byte stream that reaches a session's PTY can carry an APC sequence: a program's
own output, `cat` of a file an attacker controls, a log line, a filename in an
`ls`, a compromised or merely careless child process. Blit's own privilege
posture (ADR 0065) is that the server may run as root to reach DRM and evdev.
So `t=f` turns "whatever can print to this terminal" into "whatever can make a
root process open an arbitrary path and read it", and `t=t` and `t=s` add a
delete to that. The decode result does not even have to be rendered for this to
leak: a client can distinguish an existing path from a missing one by which
typed rejection comes back.

The one legitimate motivation for the file media is real — `t=f` and `t=t` are
how clients avoid pushing megabytes through a PTY — but Blit already answers
it on a channel that *is* authenticated.

## Options considered

1. **Implement `t=f`/`t=t` against an allowlisted root.** Accept a path only
   under a fixed, package-owned directory, as ADR 0047 does for font profiles.
   Benefit: real clients that use file transmission work. Costs: the allowlist
   has to be a real directory that some other component writes to, which means
   inventing an ownership and lifetime contract for it, and `t=t`'s mandated
   delete makes Blit a deleter of files it did not create. A path under an
   allowlisted root is still a path a hostile PTY byte stream chooses, so
   symlink traversal, TOCTOU between the check and the open, and
   `/proc`-style pseudo-files all become Blit's problem. No client of Blit
   asks for this today.
2. **Implement `t=s` only.** Shared memory avoids the filesystem. Costs: the
   same channel problem with `shm_open`, plus the mandated `shm_unlink` makes
   terminal output able to destroy another process's shared-memory object by
   name. `t=s` is also the least used of the three.
3. **Refuse every medium but `t=d`, with a typed error and a protocol reply.**
   Costs: a client that only knows how to send `t=f` cannot send an image, and
   learns so rather than being quietly mis-decoded. Benefit: no path and no
   object name that arrives through a PTY is ever opened, so the entire class
   does not exist. Large transfers have an authenticated route already.

## Decision

Option 3. `t=d` is the only transmission medium Blit accepts. `t=f`, `t=t`,
`t=s` and any future value are refused with the typed `unsupported-key`
rejection and a Kitty `EINVAL` reply, before any part of the payload is
interpreted. **No path or shared-memory object name that arrives over a
terminal channel is opened, read, created, deleted or unlinked.** That is the
invariant, and it is stated as a property of the channel rather than of the
key, so a future protocol key with the same shape inherits the answer.

Large payloads have a route: `/v1/graphics` and the pixel-frame transport over
HTTP, which is authenticated under ADR 0065 and is what Hibr's adapter uses.
A producer that needs to move a 12 MB wallpaper does not need the PTY.

Refusal is typed and answered rather than silent. A client that asks for an
unimplemented medium gets `EINVAL` immediately and can fall back, which is the
protocol's feedback loop working; a silent drop would leave it waiting, and a
quiet raw decode would draw garbage.

Reopening this needs a concrete client, and then the implementation is the
ADR 0047 shape and not a general path: a directory Blit itself creates per
session with a name the client never chooses, so the client supplies a handle
rather than a path, and `t=t`'s delete applies only to files Blit created.

## Consequences

The protocol's file and shared-memory media are unimplemented, deliberately
and visibly. `GET /v1/terminal-sessions/{id}` reports
`graphics_diagnostics.last_error = unsupported-key` and the session receives
`ESC _ G i=<id>;EINVAL ESC \`, so the refusal is diagnosable from both sides.

Blit's terminal parser performs no filesystem access on behalf of a terminal
sequence at all. That is a property worth having beyond this key: it means a
PTY stream cannot be a file-disclosure or file-deletion primitive regardless of
what the server's uid is, which keeps ADR 0065's root-capable posture from
being load-bearing here.

A client that cannot send `t=d` cannot send an image through the terminal. The
observable cost is bounded: Kitty's own clients negotiate, and Blit answers.

## Evidence

`tools/tests/terminal_conformance.rb` asserts that `t=f`, `t=t` and `t=s`
create no image and report `unsupported-key`, that `t=d` and `U=0` are
accepted, and -- the vector this ADR adds -- that `t=f` carrying the base64 of
a real, readable path is still refused and still creates no image, with the
`EINVAL` reply read back from the session's own tty. That last one is the
regression guard for the only failure mode that matters: a future
implementation that helpfully opens what it was handed.

`tools/tests/graphics_trace_gate.rb` measures the refusal as it is recorded,
on the live arrival path:

```
protocol=kitty session=session-4 bytes=42
  header=Ga=T,f=24,s=2,v=2,t=f,q=2 images=0->0 rejected=1
  reason=unsupported-key
```

Measured on 192.168.10.54 running 0.1.449, through the public API, with a
readable path in the payload:

```
t=f with a readable path  images=0 last_error="unsupported-key"
t=t with a readable path  images=0 last_error="unsupported-key"
t=s with a readable path  images=0 last_error="unsupported-key"

protocol=kitty session=session-1 bytes=54
  header=Ga=T,f=32,s=4,v=4,q=2,i=1,t=f,C=1 images=0->0 rejected=1
  reason=unsupported-key
```

Teeth: with the `t` check removed from the refusal list, the path is decoded
as pixel data and the diagnosis becomes `incomplete-data` -- Blit reporting
the wrong thing about a path it was handed, which is the shape issue #14
named. Measured on both binaries.

## Later (0.3.17): a second authenticated route

[ADR 0202](0202-local-frames-by-descriptor.md) adds the frame socket: a
program in a session sends an image's pixels as a sealed memfd, after
proving it is in that session with a random per-session token and its
uid. It keeps this record's invariant -- the descriptor is one the client
already holds, and no name from any channel is opened -- and is the answer
for local video and pages that would otherwise push megabytes through the
PTY.

