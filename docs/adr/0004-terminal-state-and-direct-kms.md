# ADR 0004: Canonical terminal state and direct KMS presentation

- Status: accepted

## Context

Blit must present Hibr sessions directly on Linux displays without X11 or
Wayland. The current prototype treats SDL3 windows and SDL debug text as the
terminal implementation. That couples terminal semantics, input, damage,
cursor handling, and presentation, and has produced unstable KMS behavior.

KMSCON demonstrates a durable separation between PTYs, terminal state, input,
fonts, cursors, DRM/KMS outputs, and page-flip scheduling. libtsm demonstrates
that a terminal emulator should own a VT state machine and a screen model,
while renderers consume that model.

## Decision

Blit will own one canonical terminal state machine and screen model per logical
pane. Hibr is a PTY/session client and never owns display rendering. The API,
PTY output, terminal parser, layout engine, and renderer are separate layers.

The authoritative presentation backend for terminal content will be a direct
DRM/KMS backend with explicit output ownership, page-flip completion, input
device ownership, and software/hardware cursor policy. SDL3 remains an adapter
for optional graphic canvases and effects; it is not the terminal state
machine or the correctness-critical text presentation path.

The first vertical slice will implement one output, one pane, one PTY, one
terminal state machine, one font backend, and one atomic frame submission. It
will be extended to joined displays only after that slice has deterministic
input, resize, damage, cursor, and recovery behavior.

Presentation cadence is refresh-aware and is a hard product invariant. Each
active output has an independent deadline derived from the maximum refresh
supported by its selected display mode. Blit must not impose a lower
frame-rate cap than that mode. Rendering faster than the mode is permitted
only as internal preparation; visible presentation remains bounded by the
output's page-flip/vblank constraints. A slower output must not throttle a
faster output.

## Hibr boundary

- Blit starts and supervises Hibr through a PTY.
- PTY bytes are parsed into Blit's canonical terminal state.
- Hibr receives keyboard, pointer, resize, and control sequences through the
  PTY according to the terminal protocol.
- Hibr lifecycle and Blit display lifecycle are correlated by API resource IDs,
  not by screen scraping or launcher heuristics.

## Consequences

Positive:

- Terminal output is deterministic and renderer-independent.
- Cursor and damage handling have one owner.
- Font sizing changes logical cell geometry and PTY size together.
- Multiple displays can join the same logical desktop without duplicating
  terminal state.
- SDL failures cannot silently redefine terminal semantics.

Costs:

- Blit needs a real terminal state machine and font/rendering boundary.
- Direct KMS requires explicit device, seat, connector, mode, and page-flip
  handling.
- Refresh-aware scheduling requires per-output deadlines, measured present
  timing, and explicit handling of outputs with different refresh rates.
- The prototype's SDL text path becomes a compatibility/test adapter rather
  than the production path.

## Licensing and reuse

KMSCON and libtsm are references and behavioral oracles for this design. Blit
will not copy implementation code without a separate licensing review. Any
third-party terminal-state dependency must be isolated behind a narrow C ABI
and recorded in the dependency manifest.

## Verification gates

The vertical slice is not complete until it demonstrates, in QEMU and on a
real DRM host:

1. stable text after PTY output stops;
2. no cursor trails during pointer motion;
3. keyboard and pointer routing to the selected pane;
4. resize updates to both logical cells and PTY dimensions;
5. renderer/device failure as a structured API diagnostic;
6. clean output recovery after VT or DRM loss;
7. under sustained damage and pointer motion, each output's effective
   scheduler cadence is at least its selected mode refresh, with scheduler
   delay distinguished from physical vblank/page-flip delay.
