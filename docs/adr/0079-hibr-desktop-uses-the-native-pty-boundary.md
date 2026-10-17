# ADR 0079: The packaged Hibr desktop uses the native PTY boundary

Native PTY output remains the decision. The historical primary-input routing
statements below are superseded by [ADR0127](0127-native-client-pty-input-and-login-owned-joins.md):
each native client receives input on its own PTY, with Hibr's own2-D rewrite.

- Status: accepted
- Date: 2026-10-15
- Scope: Hibr desktop provider selection, hold joins, terminal parsing, and
  adapter ownership
- Supersedes: [ADR 0078](0078-hibr-joins-are-independent-viewports.md)

## Context

Hibr's `hold` module fans the desktop's terminal stream out to every attached
client. A Blit-owned `console-blit` provider writes cells directly to an API
surface instead of emitting those bytes on the desktop PTY. That is suitable
for an explicitly external Hibr client, but it leaves a `desktop --join`
client with only an empty hold stream. The result is the exact failure seen on
the live host: a correct primary and a black joined pane.

Hibr already provides the tested terminal boundary used by Kitty, xterm, and
other terminal clients. Blit's terminal parser and Kitty/Sixel implementations
are the presentation boundary for that stream; the Hibr adapter is not needed
to make the packaged desktop work.

## Decision

- The packaged `blit-session.hibr` entrypoint uses Hibr's native console
  provider by default and does not load `console-blit.so` or call `console
  open` itself. Blit parses the primary PTY exactly as it parses every joined
  hold-client PTY.
- A `desktop --join` process remains a native Hibr hold client. Its local
  stream paints its own Blit surface, preserving Hibr's union-grid offsets,
  wallpaper, windows, protocol graphics, and translated input.
- Blit treats a primary Hibr session as API-owner-authored only when the API
  explicitly requests `surface_mode: external`. Such a client must complete
  the existing adapter ownership handshake. A native `terminal` primary is
  not rejected for lacking that handshake and is parsed from its PTY.
- `console-blit.so` remains a separately usable optional adapter for programs
  that explicitly need API-owned Hibr cells. It is not the provider selected
  by the packaged held desktop and must not be used to infer join behavior.
- The existing single-primary, distinct-join-surface, joined-write rejection,
  and primary-input routing rules remain in force from ADR 0078.

## Consequences

One Hibr desktop now has one true source stream and any number of local
viewports. A join cannot be blank merely because the primary provider bypassed
the PTY. Terminal scrollback, Kitty/Sixel payloads, titles, mouse reports,
and Hibr's own union geometry all cross the same boundary used by Hibr's
proven terminal clients.

The native PTY path makes the Blit terminal parser load-bearing, so parser
conformance and exact Hibr/QEMU gates are release requirements. An explicit
external adapter client remains a different mode; it must not be mixed with a
native Hibr desktop under the same surface.

## Verification

The join viewport gate runs the primary and join as terminal sessions and
proves that distinct PTY output reaches distinct surfaces, that a joined cell
write is rejected, and that pane input routes to the primary. The live
0.1.477 evidence demonstrated the diagnosis: the adapter-primary surface
painted while its join stayed blank, whereas a native-Hibr probe produced
joined PTY cells. Release 0.1.478 must rerun the exact live two-pane capture
and show both the primary and joined surfaces populated before claiming the
fix.
