# ADR 0097: Reassert the Hibr terminal contract after authentication

- Status: accepted
- Date: 2026-10-16
- Scope: Hibr login integration, Kitty/Sixel negotiation, terminal rendering
- Related: [ADR 0083](0083-blit-root-owned-hibr-login-manager.md), [ADR 0096](0096-kitty-apc-buffer-carries-the-full-hibr-chunk.md)

## Context

The Blit service starts its login manager with `HIBR_GFX=kitty`, but Hibr's
root authentication boundary intentionally clears the environment before it
executes the selected user's desktop. The authenticated desktop therefore
lost `HIBR_GFX`, `DT_BIDI`, and `COLORTERM`, even though the Blit PTY reported
the Kitty-capable terminal contract. Hibr then selected its half-block image
path. The live trace consequently contained a Kitty capability query and no
image upload, while the canonical cell surface contained the half-block
fallback and leaked truecolor control text.

This is a Blit integration boundary. Hibr's authentication behavior and
source remain unchanged.

## Decision

The Blit-owned `hibr/blit-session.hibr` entrypoint reasserts the terminal
contract after the authentication boundary:

- `HIBR_GFX=kitty` by default;
- `DT_BIDI=on` by default;
- `COLORTERM=truecolor` by default.

Each value is assigned only when absent, so an explicit API or controlled
fixture override remains authoritative. All Hibr desktop launches, including
login-selected desktops and joins, pass through this one entrypoint.

## Verification

The package gate requires the entrypoint to contain all three post-login
defaults. The terminal conformance gate verifies Hibr's combined truecolor
SGR across a fragmented PTY write. Live acceptance additionally requires:

1. `/v1/terminal-sessions/{id}` to report the expected terminal contract;
2. `/v1/diagnostics/trace?kind=graphics_payload` to contain a complete Kitty
   image upload, not only a capability query or delete;
3. `graphics_texture_uploads` to increase and the screenshot to show the
   decoded image without half-block substitution.

## Consequences

The authenticated desktop uses the same graphics negotiation as an API-created
desktop. A user-selected `HIBR_GFX=sixel` or `HIBR_GFX=off` remains possible,
and the API continues to report the actual Blit parser capability separately
from the Hibr client's selected mode.
